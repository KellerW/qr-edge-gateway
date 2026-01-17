/* SPDX-License-Identifier: CC-BY-NC-4.0 */
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
 * Lifetime expectations:
 * - The referenced dependencies (Crow app, Core, Dispatcher, JobRunner, JobStore)
 *   must outlive this RestServer instance.
 */

#pragma once

#include <crow.h>

#include <string>

#include "app/Dispatcher.hpp"
#include "app/JobRunner.hpp"
#include "app/JobStore.hpp"
#include "core/Core.hpp"

namespace adapters::rest
{

class RestServer
{
  public:
    RestServer(crow::SimpleApp& app,
               core::Core& core,
               app::Dispatcher& dispatcher,
               app::JobRunner& jobs,
               app::JobStore& store,
               std::string bind_addr,
               int port,
               int default_timeout_ms);

    void setup_routes();
    void run();

  private:
    crow::SimpleApp& app_;
    core::Core& core_;
    app::Dispatcher& dispatcher_;
    app::JobRunner& jobs_;
    app::JobStore& store_;

    std::string bind_addr_;
    int port_{0};
    int default_timeout_ms_{3000};
};

} // namespace adapters::rest
