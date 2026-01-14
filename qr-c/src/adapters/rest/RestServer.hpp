/**
 * @file RestServer.hpp
 * @brief REST server adapter interface based on Crow.
 *
 * This header declares adapters::rest::RestServer, a thin HTTP layer that:
 * - registers REST routes on a Crow application instance (crow::SimpleApp),
 * - bridges HTTP requests to the domain layer (core::Core) and application services
 *   (app::Dispatcher, app::JobRunner, app::JobStore),
 * - runs the HTTP server loop (bind + port + multithreaded).
 *
 * Typical usage:
 * @code{.cpp}
 * crow::SimpleApp app;
 * core::Core core;
 * app::Dispatcher dispatcher(core);
 * app::JobStore store;
 * app::JobRunner jobs(store);
 *
 * adapters::rest::RestServer server(app, core, dispatcher, jobs, store, "0.0.0.0", 8080, 3000);
 * server.setup_routes();
 * server.run();
 * @endcode
 */

#pragma once

#include "crow.h"

#include <string>

// Adjust include paths: RestServer.hpp is located in src/adapters/rest/
#include "../../app/Dispatcher.hpp"
#include "../../app/JobRunner.hpp"
#include "../../app/JobStore.hpp"
#include "../../core/Core.hpp"

namespace adapters::rest
{

/**
 * @class RestServer
 * @brief Registers and serves the project's REST API using Crow.
 *
 * Responsibilities:
 * - Owns no business logic; it only adapts HTTP requests/responses to/from the
 *   application layer.
 * - Exposes route registration via setup_routes().
 * - Starts the server loop via run().
 *
 * Lifetime expectations:
 * - The referenced dependencies (Crow app, Core, Dispatcher, JobRunner, JobStore)
 *   must outlive this RestServer instance.
 */
class RestServer
{
  public:
    /**
     * @brief Constructs the REST server adapter.
     *
     * @param app                Crow application instance used to register routes.
     * @param core               Core domain object used for reading state and stringifying it.
     * @param dispatcher         Application dispatcher responsible for executing commands.
     * @param jobs               Job runner used to start asynchronous jobs.
     * @param store              Job store used to query job existence and results.
     * @param bind_addr          Bind address/interface (e.g., "0.0.0.0").
     * @param port               TCP port used to listen for HTTP requests.
     * @param default_timeout_ms Default timeout (ms) used by endpoints that accept an optional timeout.
     */
    RestServer(crow::SimpleApp& app, core::Core& core, app::Dispatcher& dispatcher, app::JobRunner& jobs,
               app::JobStore& store, std::string bind_addr, int port, int default_timeout_ms);

    /**
     * @brief Registers all REST routes on the Crow application.
     *
     * This method must be called before run().
     */
    void setup_routes();

    /**
     * @brief Starts the HTTP server loop (blocking).
     *
     * Binds to the configured address and port and runs in multithreaded mode.
     */
    void run();

  private:
    /// Reference to the Crow application instance (route registration + server runtime).
    crow::SimpleApp& app_;

    /// Reference to the domain core state machine / state provider.
    core::Core& core_;

    /// Reference to the dispatcher used to submit synchronous and job-related commands.
    app::Dispatcher& dispatcher_;

    /// Reference to the job runner responsible for starting asynchronous work.
    app::JobRunner& jobs_;

    /// Reference to the job store used to query job existence and retrieve results.
    app::JobStore& store_;

    /// Bind address/interface for the HTTP server.
    std::string bind_addr_;

    /// TCP port for the HTTP server.
    int port_{0};

    /// Default timeout (ms) used when a request does not provide one.
    int default_timeout_ms_{3000};
};

} // namespace adapters::rest
