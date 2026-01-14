/**
 * @file main.cpp
 * @brief Application entry point: initializes Core, Dispatcher, JobStore/JobRunner, and the REST server (Crow).
 *
 * This binary reads environment variables to configure bind address/port/timeouts and
 * starts an HTTP REST server using Crow, delegating route registration to
 * adapters::rest::RestServer.
 *
 * Supported environment variables:
 * - REST_BIND: bind address/interface (default: "0.0.0.0")
 * - REST_PORT: TCP port (default: 8080)
 * - READ_TIMEOUT_MS: read timeout in ms (default: 3000)
 */

#include "adapters/rest/RestServer.hpp"
#include "app/Dispatcher.hpp"
#include "app/JobRunner.hpp"
#include "app/JobStore.hpp"
#include "core/Core.hpp"
#include "crow.h"

#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <string>

/**
 * @brief Reads an environment variable as a string, with fallback.
 *
 * Returns the value of @p k if it exists and is not empty; otherwise returns @p def.
 *
 * @param k   Environment variable name (e.g., "REST_BIND").
 * @param def Default value to use when the variable is missing or empty.
 * @return std::string Resolved value (env or default).
 */
static std::string getenv_str(const char* k, const std::string& def)
{
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : def;
}

/**
 * @brief Reads an environment variable as an unsigned 32-bit integer, with fallback.
 *
 * Uses std::strtoul for parsing and basic validation. If parsing fails, overflows,
 * or the variable is missing/empty, returns @p def.
 *
 * @param k   Environment variable name (e.g., "READ_TIMEOUT_MS").
 * @param def Default value to use when the variable is missing/invalid.
 * @return std::uint32_t Resolved value (parsed env or default).
 */
static std::uint32_t getenv_u32(const char* k, std::uint32_t def)
{
    const char* v = std::getenv(k);
    if (!v || !*v)
        return def;

    errno = 0;
    char* end = nullptr;
    const unsigned long val = std::strtoul(v, &end, 10);

    if (errno != 0 || end == v || *end != '\0')
        return def;

    if (val > static_cast<unsigned long>(UINT32_MAX))
        return def;

    return static_cast<std::uint32_t>(val);
}

/**
 * @brief Reads an environment variable as an unsigned 16-bit integer, with fallback.
 *
 * Intended for TCP ports. If parsing fails, overflows, or the variable is missing/empty,
 * returns @p def.
 *
 * @param k   Environment variable name (e.g., "REST_PORT").
 * @param def Default value to use when the variable is missing/invalid.
 * @return std::uint16_t Resolved value (parsed env or default).
 */
static std::uint16_t getenv_u16(const char* k, std::uint16_t def)
{
    const std::uint32_t val = getenv_u32(k, static_cast<std::uint32_t>(def));
    if (val > static_cast<std::uint32_t>(UINT16_MAX))
        return def;
    return static_cast<std::uint16_t>(val);
}

/**
 * @brief Process entry point.
 *
 * Initialization flow:
 * 1. Reads configuration (bind address, port, timeout) from environment variables.
 * 2. Creates the Crow HTTP application (crow::SimpleApp).
 * 3. Initializes domain components:
 *    - core::Core
 *    - app::Dispatcher (depends on core::Core)
 *    - app::JobStore and app::JobRunner
 * 4. Creates the REST server (adapters::rest::RestServer), registers routes, and starts the server loop.
 *
 * @return int Process exit code (0 on normal execution until termination).
 */
int main()
{
    const std::string bind = getenv_str("REST_BIND", "0.0.0.0");
    const std::uint16_t port = getenv_u16("REST_PORT", static_cast<std::uint16_t>(8080u));
    const std::uint32_t timeout_ms = getenv_u32("READ_TIMEOUT_MS", static_cast<std::uint32_t>(3000u));

    crow::SimpleApp app;

    core::Core core;
    app::Dispatcher dispatcher(core);

    app::JobStore store;
    app::JobRunner jobs(store);

    adapters::rest::RestServer server(
        app,
        core,
        dispatcher,
        jobs,
        store,
        bind,
        static_cast<int>(port),
        static_cast<int>(timeout_ms));

    server.setup_routes();
    server.run();
    return 0;
}
