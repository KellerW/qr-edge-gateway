#include "JobRunner.hpp"

#include <chrono>
#include <random>
#include <vector>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <termios.h>

#include <spdlog/spdlog.h>

namespace app
{

static std::string gen_id()
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i)
        s.push_back(hex[rd() % 16]);
    return s;
}

// -------------------- COBS decode --------------------
static bool cobs_decode(const std::uint8_t* input, std::size_t length, std::vector<std::uint8_t>& output)
{
    output.clear();
    output.reserve(length);

    std::size_t i = 0;
    while (i < length)
    {
        const std::uint8_t code = input[i];
        if (code == 0)
            return false;
        ++i;

        const std::size_t copy_len = static_cast<std::size_t>(code) - 1U;
        if (i + copy_len > length)
            return false;

        for (std::size_t j = 0; j < copy_len; ++j)
            output.push_back(input[i + j]);

        i += copy_len;

        if (code != 0xFF && i < length)
            output.push_back(0x00);
    }

    return true;
}

// -------------------- Baudrate config (best-effort) --------------------
static speed_t baud_to_speed(int baud)
{
    switch (baud)
    {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return static_cast<speed_t>(0);
    }
}

static void configure_serial_best_effort(int fd, int baudrate)
{
    const speed_t spd = baud_to_speed(baudrate);
    if (spd == 0)
    {
        spdlog::warn("unsupported baudrate={} (termios), skipping", baudrate);
        return;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) != 0)
    {
        spdlog::warn("tcgetattr failed: {}", std::strerror(errno));
        return;
    }

    cfmakeraw(&tio);
    (void)cfsetispeed(&tio, spd);
    (void)cfsetospeed(&tio, spd);

    // 8N1
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        spdlog::warn("tcsetattr failed: {}", std::strerror(errno));
        return;
    }

    spdlog::info("serial configured: baudrate={}", baudrate);
}

// -------------------- utils --------------------
static int getenv_int(const char* key, int defv)
{
    const char* v = std::getenv(key);
    if (!v || !*v)
        return defv;

    char* end = nullptr;
    const long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0')
        return defv;
    if (x < 0 || x > 600000)
        return defv;
    return static_cast<int>(x);
}

// Reads bytes until 0x00 delimiter appears, or timeout is reached.
// encoded_frame contains the COBS payload WITHOUT the final 0x00.
static bool read_cobs_frame(int fd,
                            int timeout_ms,
                            std::atomic<bool>& running_flag,
                            std::vector<std::uint8_t>& encoded_frame)
{
    encoded_frame.clear();

    const int step_ms = 50;
    int remaining = timeout_ms;

    std::uint8_t buf[256];

    while (remaining > 0 && running_flag.load())
    {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        const int wait_ms = (remaining < step_ms) ? remaining : step_ms;
        const int rc = ::poll(&pfd, 1, wait_ms);

        if (!running_flag.load())
            return false;

        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            spdlog::warn("poll() failed: {}", std::strerror(errno));
            return false;
        }

        if (rc == 0)
        {
            remaining -= wait_ms;
            continue;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            spdlog::warn("poll revents error: revents=0x{:x}", static_cast<unsigned>(pfd.revents));
            return false;
        }

        if (pfd.revents & POLLIN)
        {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    remaining -= wait_ms;
                    continue;
                }
                spdlog::warn("read() failed: {}", std::strerror(errno));
                return false;
            }

            if (n == 0)
            {
                spdlog::warn("read() returned EOF");
                return false;
            }

            for (ssize_t i = 0; i < n; ++i)
            {
                if (buf[i] == 0x00)
                {
                    return !encoded_frame.empty();
                }

                encoded_frame.push_back(buf[i]);

                if (encoded_frame.size() > 4096)
                {
                    spdlog::warn("frame too large (>4096), discarding");
                    return false;
                }
            }
        }

        remaining -= wait_ms;
    }

    return false; // timeout or cancelled
}

std::string JobRunner::start(int timeout_ms)
{
    stop();

    const std::string id = gen_id();
    store_.create(id);

    running_ = true;
    worker_ = std::thread(&JobRunner::run_job, this, id, timeout_ms);
    return id;
}

void JobRunner::run_job(std::string id, int timeout_ms)
{
    using namespace std::chrono;

    const int reopen_delay_ms = getenv_int("REOPEN_DELAY_MS", 1000);
    const auto deadline = steady_clock::now() + milliseconds(timeout_ms);

    spdlog::info("job {} started (timeout_ms={}, serial_port='{}', baudrate={}, reopen_delay_ms={})",
                 id, timeout_ms, serial_port_, baudrate_.load(), reopen_delay_ms);

    std::vector<std::uint8_t> enc;
    std::vector<std::uint8_t> dec;

    while (running_.load())
    {
        const auto now = steady_clock::now();
        if (now >= deadline)
        {
            spdlog::info("job {} timeout (deadline reached before frame)", id);
            store_.set_timeout(id);
            running_ = false;
            (void)dispatcher_.submit_finish_job();
            return;
        }

        const auto remaining_ms =
            static_cast<int>(duration_cast<milliseconds>(deadline - now).count());

        // Try opening serial
        const int fd = ::open(serial_port_.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (fd < 0)
        {
            // Retry open until deadline (requirement: handle reopen)
            spdlog::warn("job {} open('{}') failed: {} (retry in {}ms)",
                         id, serial_port_, std::strerror(errno), reopen_delay_ms);

            const int sleep_ms = (reopen_delay_ms < remaining_ms) ? reopen_delay_ms : remaining_ms;
            if (sleep_ms > 0)
                std::this_thread::sleep_for(milliseconds(sleep_ms));
            continue;
        }

        // Configure (best-effort)
        configure_serial_best_effort(fd, baudrate_.load());

        // Read one frame with remaining time budget
        const bool got_frame = read_cobs_frame(fd, remaining_ms, running_, enc);
        ::close(fd);

        if (!running_.load())
        {
            spdlog::info("job {} cancelled", id);
            store_.set_cancelled(id);
            running_ = false;
            (void)dispatcher_.submit_finish_job();
            return;
        }

        if (!got_frame)
        {
            // No frame yet; go around the loop and possibly reopen if bridge dropped.
            // If deadline is close, the loop will TIMEOUT at top.
            spdlog::debug("job {} no complete frame yet; retrying until deadline", id);

            // Small sleep to avoid tight loop if read failed quickly
            std::this_thread::sleep_for(milliseconds(20));
            continue;
        }

        // Decode frame
        if (!cobs_decode(enc.data(), enc.size(), dec))
        {
            spdlog::warn("job {} invalid COBS frame; continuing until deadline", id);
            // Continue until deadline (do not instantly fail)
            std::this_thread::sleep_for(milliseconds(20));
            continue;
        }

        const std::string payload(dec.begin(), dec.end());
        spdlog::info("job {} got payload='{}'", id, payload);

        if (payload.rfind("QR:", 0) == 0)
        {
            store_.set_done(id, payload); // store full "QR:123456"
            running_ = false;
            (void)dispatcher_.submit_finish_job();
            return;
        }

        // Payload not recognized; keep trying until deadline
        spdlog::warn("job {} unexpected payload; continuing until deadline", id);
        std::this_thread::sleep_for(milliseconds(20));
    }

    // If we exit the loop due to stop():
    spdlog::info("job {} cancelled (stop requested)", id);
    store_.set_cancelled(id);
    running_ = false;
    (void)dispatcher_.submit_finish_job();
}

void JobRunner::stop()
{
    running_ = false;
    if (worker_.joinable())
        worker_.join();
}

void JobRunner::set_baudrate(int baudrate)
{
    if (baudrate <= 0)
        return;
    baudrate_.store(baudrate);
    spdlog::info("baudrate updated to {}", baudrate);
}

} // namespace app
