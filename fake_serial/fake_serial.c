// fake_serial.c - TCP server emitting COBS-framed parcels: COBS(payload) + 0x00
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int sig) { (void)sig; g_stop = 1; }

static int getenv_int(const char* key, int defv)
{
    const char* v = getenv(key);
    if (!v || !*v) return defv;
    char* end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v || *end != '\0') return defv;
    if (x < 0 || x > 1000000) return defv;
    return (int)x;
}

// COBS encode. out must fit worst-case: in_len + in_len/254 + 1
static size_t cobs_encode(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap)
{
    if (!out || out_cap == 0) return 0;

    size_t read_index = 0;
    size_t write_index = 1;
    size_t code_index = 0;
    uint8_t code = 1;

    if (out_cap < 2 && in_len > 0) return 0;

    while (read_index < in_len)
    {
        if (in[read_index] == 0)
        {
            out[code_index] = code;
            code = 1;
            code_index = write_index++;
            read_index++;
            if (write_index >= out_cap) return 0;
        }
        else
        {
            if (write_index >= out_cap) return 0;
            out[write_index++] = in[read_index++];
            code++;
            if (code == 0xFF)
            {
                out[code_index] = code;
                code = 1;
                code_index = write_index++;
                if (write_index >= out_cap) return 0;
            }
        }
    }

    out[code_index] = code;
    return write_index;
}

static ssize_t send_all(int fd, const uint8_t* buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return (ssize_t)off;
}

static void msleep(int ms)
{
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

int main(void)
{
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    const int port = getenv_int("FAKE_SERIAL_PORT", 7000);
    const int interval_ms = getenv_int("PARCEL_INTERVAL_MS", 2000);

    const char* payload_env = getenv("PARCEL_PAYLOAD");
    const char* payload = (payload_env && *payload_env) ? payload_env : "QR:123456";

    fprintf(stderr,
            "[fake-serial] listen 0.0.0.0:%d interval=%dms payload='%s'\n",
            port, interval_ms, payload);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int yes = 1;
    (void)setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) { perror("bind"); close(srv); return 1; }
    if (listen(srv, 8) != 0) { perror("listen"); close(srv); return 1; }

    while (!g_stop)
    {
        struct sockaddr_in cli;
        socklen_t cli_len = (socklen_t)sizeof(cli);
        int cfd = accept(srv, (struct sockaddr*)&cli, &cli_len);
        if (cfd < 0)
        {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char ip[64];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        fprintf(stderr, "[fake-serial] client %s:%d connected\n", ip, ntohs(cli.sin_port));

        while (!g_stop)
        {
            const uint8_t* in = (const uint8_t*)payload;
            const size_t in_len = strlen(payload);

            uint8_t enc[1024];
            if (in_len + (in_len / 254) + 2 > sizeof(enc))
            {
                fprintf(stderr, "[fake-serial] payload too large\n");
                break;
            }

            size_t enc_len = cobs_encode(in, in_len, enc, sizeof(enc));
            if (enc_len == 0)
            {
                fprintf(stderr, "[fake-serial] cobs_encode failed\n");
                break;
            }

            enc[enc_len++] = 0x00; // frame delimiter

            if (send_all(cfd, enc, enc_len) < 0)
            {
                perror("[fake-serial] send");
                break;
            }

            msleep(interval_ms);
        }

        close(cfd);
        fprintf(stderr, "[fake-serial] client disconnected\n");
    }

    close(srv);
    fprintf(stderr, "[fake-serial] stopping\n");
    return 0;
}
