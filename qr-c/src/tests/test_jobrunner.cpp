#include <catch2/catch_test_macros.hpp>

#include "../core/Core.hpp"
#include "../app/Dispatcher.hpp"
#include "../app/JobStore.hpp"
#include "../app/JobRunner.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

static std::vector<std::uint8_t> cobs_encode(const std::uint8_t* input, std::size_t length)
{
    // Standard COBS encoder (no trailing delimiter here)
    std::vector<std::uint8_t> out;
    out.reserve(length + 2);

    std::size_t code_index = 0;
    std::uint8_t code = 1;

    out.push_back(0); // placeholder for code

    for (std::size_t i = 0; i < length; ++i)
    {
        const std::uint8_t b = input[i];
        if (b == 0)
        {
            out[code_index] = code;
            code_index = out.size();
            out.push_back(0); // placeholder
            code = 1;
        }
        else
        {
            out.push_back(b);
            ++code;
            if (code == 0xFF)
            {
                out[code_index] = code;
                code_index = out.size();
                out.push_back(0); // placeholder
                code = 1;
            }
        }
    }

    out[code_index] = code;
    return out;
}

struct PtyPair
{
    int master_fd{-1};
    std::string slave_path;

    PtyPair()
    {
        master_fd = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master_fd >= 0);

        REQUIRE(::grantpt(master_fd) == 0);
        REQUIRE(::unlockpt(master_fd) == 0);

        char* p = ::ptsname(master_fd);
        REQUIRE(p != nullptr);
        slave_path = p;
    }

    ~PtyPair()
    {
        if (master_fd >= 0)
            ::close(master_fd);
    }

    PtyPair(const PtyPair&) = delete;
    PtyPair& operator=(const PtyPair&) = delete;
};

static void write_cobs_frame(int fd, const std::string& payload)
{
    const auto enc = cobs_encode(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());

    std::vector<std::uint8_t> frame;
    frame.reserve(enc.size() + 1);
    frame.insert(frame.end(), enc.begin(), enc.end());
    frame.push_back(0x00); // delimiter

    const ssize_t n = ::write(fd, frame.data(), frame.size());
    REQUIRE(n == static_cast<ssize_t>(frame.size()));
}

static app::JobStatus wait_until_done(app::JobStore& store,
                                     const std::string& jobId,
                                     std::chrono::milliseconds max_wait)
{
    const auto deadline = std::chrono::steady_clock::now() + max_wait;

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto r = store.get(jobId);
        if (r.status != app::JobStatus::PENDING)
            return r.status;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return store.get(jobId).status;
}

TEST_CASE("JobRunner: DONE when a valid QR payload arrives", "[jobrunner]")
{
    PtyPair pty;

    core::Core core;
    app::Dispatcher dispatcher(core);
    app::JobStore store;

    // Bring core to INIT then RUNNING (same flow as REST /start)
    REQUIRE(dispatcher.submit_sync("INIT").get().ok);
    REQUIRE(dispatcher.submit_start_job(1000).get().ok);

    app::JobRunner runner(store, dispatcher, pty.slave_path);
    runner.set_baudrate(115200);

    const std::string jobId = runner.start(1000);

    // Simulate device frame arriving
    write_cobs_frame(pty.master_fd, "QR:123456");

    const auto st = wait_until_done(store, jobId, std::chrono::milliseconds(1500));
    REQUIRE(st == app::JobStatus::DONE);

    const auto res = store.get(jobId);
    REQUIRE(res.qr.has_value());
    REQUIRE(res.qr.value() == "QR:123456");
}

TEST_CASE("JobRunner: TIMEOUT when no frame arrives", "[jobrunner]")
{
    PtyPair pty;

    core::Core core;
    app::Dispatcher dispatcher(core);
    app::JobStore store;

    REQUIRE(dispatcher.submit_sync("INIT").get().ok);
    REQUIRE(dispatcher.submit_start_job(150).get().ok);

    app::JobRunner runner(store, dispatcher, pty.slave_path);

    const std::string jobId = runner.start(150);

    const auto st = wait_until_done(store, jobId, std::chrono::milliseconds(800));
    REQUIRE(st == app::JobStatus::TIMEOUT);
}

TEST_CASE("JobRunner: CANCELLED when stopped while waiting", "[jobrunner]")
{
    PtyPair pty;

    core::Core core;
    app::Dispatcher dispatcher(core);
    app::JobStore store;

    REQUIRE(dispatcher.submit_sync("INIT").get().ok);
    REQUIRE(dispatcher.submit_start_job(2000).get().ok);

    app::JobRunner runner(store, dispatcher, pty.slave_path);

    const std::string jobId = runner.start(2000);

    // Give the thread a moment to enter poll/read, then cancel
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.stop();

    const auto st = wait_until_done(store, jobId, std::chrono::milliseconds(1000));
    REQUIRE(st == app::JobStatus::CANCELLED);
}
