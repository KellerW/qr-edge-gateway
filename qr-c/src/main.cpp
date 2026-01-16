/**
 * @file main.cpp
 * @brief Application entry point: initializes Core, Dispatcher, JobStore/JobRunner, and the REST server (Crow).
 *
 * This binary accepts CLI options (CLI11) and may also read environment variables
 * to configure bind address/port/timeouts. Precedence is:
 * CLI arguments > environment variables > defaults.
 *
 * Container-friendly logging:
 * - Logs go to stdout/stderr by default (recommended for Docker/Compose/K8s).
 * - Optional rotating file logging can be enabled explicitly via --log-file or LOG_FILE
 *   (use only with a mounted volume).
 *
 * Supported options / environment variables:
 * - --host (env: REST_BIND)              default: "0.0.0.0"
 * - -p/--port (env: REST_PORT)           default: 8080
 * - --timeout-ms (env: READ_TIMEOUT_MS)  default: 3000
 * - --log-level (env: LOG_LEVEL)         default: "info"  (debug|info|warn|error)
 * - --log-file  (env: LOG_FILE)          default: ""      (disabled)
 * - -v/--verbose                          sets log level to debug unless LOG_LEVEL is provided
 * - (env) SERIAL_PORT                     default: "/tmp/ttyS1"
 */

#include "adapters/rest/RestServer.hpp"
#include "app/Dispatcher.hpp"
#include "app/JobRunner.hpp"
#include "app/JobStore.hpp"
#include "core/Core.hpp"

#include <CLI/CLI.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdint>
#include <cstdlib>   // std::getenv
#include <memory>
#include <string>
#include <vector>

struct Config
{
    std::string host;
    std::uint16_t port{8080};
    std::uint32_t timeout_ms{3000};

    bool verbose{false};

    // Logging (container-first)
    std::string log_level{"info"}; // debug|info|warn|error
    std::string log_file{};        // empty => file logging disabled
};

static spdlog::level::level_enum parse_log_level(const std::string& s, bool verbose_fallback)
{
    if (s == "debug") return spdlog::level::debug;
    if (s == "info")  return spdlog::level::info;
    if (s == "warn")  return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;

    return verbose_fallback ? spdlog::level::debug : spdlog::level::info;
}

static void init_logging(const Config& cfg)
{
    std::vector<spdlog::sink_ptr> sinks;

    // Always log to console (stdout/stderr). This is the recommended container pattern.
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // Optional: also log to a rotating file if explicitly enabled.
    if (!cfg.log_file.empty())
    {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.log_file, 5 * 1024 * 1024, 3)); // 5MB, keep 3 files
    }

    auto logger = std::make_shared<spdlog::logger>("qr_c", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);

    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [t%t] %v");
    spdlog::set_level(parse_log_level(cfg.log_level, cfg.verbose));
    spdlog::flush_on(spdlog::level::info);
}

static std::string getenv_str(const char* key, const char* defv)
{
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(defv);
}

int main(int argc, char* argv[])
{
    Config cfg;

    CLI::App cli{"qr_c"};

    cli.add_option("--host", cfg.host, "Bind address")
        ->envname("REST_BIND")
        ->default_val("0.0.0.0");

    cli.add_option("-p,--port", cfg.port, "Port")
        ->envname("REST_PORT")
        ->check(CLI::Range(1, 65535))
        ->default_val(std::uint16_t{8080});

    cli.add_option("--timeout-ms", cfg.timeout_ms, "Read timeout (ms)")
        ->envname("READ_TIMEOUT_MS")
        ->check(CLI::NonNegativeNumber)
        ->default_val(std::uint32_t{3000});

    cli.add_flag("-v,--verbose", cfg.verbose, "Verbose logs (debug)");

    cli.add_option("--log-level", cfg.log_level, "Log level: debug|info|warn|error")
        ->envname("LOG_LEVEL")
        ->default_val("info");

    // Disabled by default for containers; enable explicitly if you mount a volume.
    cli.add_option("--log-file", cfg.log_file, "Optional log file path (enable only with a mounted volume)")
        ->envname("LOG_FILE")
        ->default_val("");

    CLI11_PARSE(cli, argc, argv);

    init_logging(cfg);

    const std::string serial_port = getenv_str("SERIAL_PORT", "/tmp/ttyS1");

    spdlog::info("Starting qr_c on {}:{} timeout={}ms serial_port='{}'",
                 cfg.host, cfg.port, cfg.timeout_ms, serial_port);
    spdlog::debug("Logging configured: level='{}' file='{}'", cfg.log_level, cfg.log_file);

    crow::SimpleApp crow_app;

    core::Core core;
    app::Dispatcher dispatcher(core);

    app::JobStore store;
    // IMPORTANT: JobRunner now needs Dispatcher + SERIAL_PORT for COBS parcel reading.
    app::JobRunner jobs(store, dispatcher, serial_port);

    adapters::rest::RestServer server(
        crow_app,
        core,
        dispatcher,
        jobs,
        store,
        cfg.host,
        static_cast<int>(cfg.port),
        static_cast<int>(cfg.timeout_ms));

    server.setup_routes();

    spdlog::info("REST server initialized; entering run loop");
    server.run();

    spdlog::info("REST server stopped");
    return 0;
}
