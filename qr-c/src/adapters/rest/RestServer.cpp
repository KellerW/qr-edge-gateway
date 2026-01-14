/**
 * @file RestServer.cpp
 * @brief REST API adapter implemented with Crow.
 *
 * This module exposes HTTP endpoints that wrap the application core and job subsystem:
 * - Health probing
 * - Core state reporting
 * - Synchronous command dispatch
 * - Asynchronous job start + result polling
 *
 * All responses are JSON with `Content-Type: application/json`.
 *
 * Endpoints:
 * - GET  /health                -> { ok, message }
 * - GET  /status                -> { ok, state }
 * - POST /command               -> { ok, command, state, message, data:{ qr?, jobId? } }
 * - POST /start                 -> { ok, jobId, state, message } (202 Accepted)
 * - GET  /result/{jobId}        -> { ok, jobId, status, state, message, data:{ qr? } }
 */

#include "RestServer.hpp"

#include <chrono>

namespace adapters::rest
{

/**
 * @brief Converts an app::JobStatus enum to a stable string for JSON output.
 *
 * @param s Job status value.
 * @return std::string A stable, upper-case status string ("PENDING", "DONE", "TIMEOUT"),
 *         or "UNKNOWN" if the enum value is not recognized.
 */
static std::string job_status_to_string(app::JobStatus s)
{
    switch (s)
    {
    case app::JobStatus::PENDING:
        return "PENDING";
    case app::JobStatus::DONE:
        return "DONE";
    case app::JobStatus::TIMEOUT:
        return "TIMEOUT";
    }
    return "UNKNOWN";
}

/**
 * @brief Builds a JSON HTTP response with the correct Content-Type.
 *
 * Crow (in this version) accepts `crow::response(int, std::string)`. This helper
 * serializes the JSON (`out.dump()`) and ensures the `Content-Type` header is set
 * to `application/json`.
 *
 * @param code HTTP status code to return.
 * @param out  JSON payload to serialize.
 * @return crow::response Fully-initialized Crow response.
 */
static crow::response json_response(int code, const crow::json::wvalue& out)
{
    crow::response res(code, out.dump());
    res.set_header("Content-Type", "application/json");
    return res;
}

/**
 * @brief Ensures that `out["data"]` is always a JSON object and never null.
 *
 * Several API responses optionally attach fields under `data` (e.g., `qr`, `jobId`).
 * To keep the response schema stable, this helper initializes `data` as `{}`.
 *
 * @param out JSON object to mutate.
 */
static void ensure_data_object(crow::json::wvalue& out)
{
    out["data"] = crow::json::wvalue::empty_object();
}

/**
 * @brief Constructs the REST server adapter.
 *
 * @param app                Crow application instance used to register routes.
 * @param core               Core domain object.
 * @param dispatcher         Command/job dispatcher facade.
 * @param jobs               Job runner that starts asynchronous jobs.
 * @param store              Job store used for status/result retrieval.
 * @param bind_addr          Bind address/interface (e.g., "0.0.0.0").
 * @param port               TCP port for the HTTP server.
 * @param default_timeout_ms Default job timeout used by /start when not provided.
 */
RestServer::RestServer(crow::SimpleApp& app, core::Core& core, app::Dispatcher& dispatcher, app::JobRunner& jobs,
                       app::JobStore& store, std::string bind_addr, int port, int default_timeout_ms)
    : app_(app), core_(core), dispatcher_(dispatcher), jobs_(jobs), store_(store), bind_addr_(std::move(bind_addr)),
      port_(port), default_timeout_ms_(default_timeout_ms)
{
}

/**
 * @brief Registers all REST routes on the Crow application.
 *
 * Routes and behavior:
 *
 * - GET /health
 *   Returns a simple liveness response: `{ ok: true, message: "UP" }`.
 *
 * - GET /status
 *   Returns the current core state: `{ ok: true, state: "<STATE>" }`.
 *
 * - POST /command
 *   Body: `{ "command": "<string>" }`
 *   Submits a synchronous command via Dispatcher and waits up to 500ms for readiness.
 *   - 400 if body is invalid or missing required fields
 *   - 503 if the dispatcher is busy (future not ready within 500ms)
 *   - otherwise returns `core::Response.http_status` and a normalized JSON payload:
 *     `{ ok, command, state, message, data:{ qr?, jobId? } }`
 *
 * - POST /start
 *   Optional body: `{ "timeout_ms": <number> }`
 *   Validates timeout, submits a "start job" command via Dispatcher and waits up to 500ms.
 *   - 400 if body is invalid or timeout is invalid (<= 0 or wrong type)
 *   - 503 if dispatcher is busy
 *   - if accepted, starts the asynchronous job via JobRunner and returns 202:
 *     `{ ok: true, jobId, state, message: "ACCEPTED" }`
 *
 * - GET /result/{id}
 *   Polls job state from JobStore:
 *   - 404 if jobId does not exist
 *   - 200 with `{ ok: true, jobId, status, state, message, data:{ qr? } }`
 */
void RestServer::setup_routes()
{
    CROW_ROUTE(app_, "/health")
        .methods(crow::HTTPMethod::GET)(
            []
            {
                crow::json::wvalue out;
                out["ok"] = true;
                out["message"] = "UP";
                return json_response(200, out);
            });

    CROW_ROUTE(app_, "/status")
        .methods(crow::HTTPMethod::GET)(
            [this]
            {
                crow::json::wvalue out;
                out["ok"] = true;
                out["state"] = core::to_string(core_.state());
                return json_response(200, out);
            });

    CROW_ROUTE(app_, "/command")
        .methods(crow::HTTPMethod::POST)(
            [this](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                if (!body || !body.has("command"))
                {
                    crow::json::wvalue out;
                    out["ok"] = false;
                    out["message"] = "ERR:BAD_REQUEST";
                    return json_response(400, out);
                }

                const std::string cmd = body["command"].s();

                auto fut = dispatcher_.submit_sync(cmd);
                if (fut.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
                {
                    crow::json::wvalue out;
                    out["ok"] = false;
                    out["message"] = "ERR:BUSY";
                    return json_response(503, out);
                }

                const core::Response r = fut.get();

                crow::json::wvalue out;
                out["ok"] = r.ok;
                out["command"] = r.command;
                out["state"] = core::to_string(r.state);
                out["message"] = r.message;

                ensure_data_object(out);
                if (r.qr.has_value())
                    out["data"]["qr"] = *r.qr;
                if (r.jobId.has_value())
                    out["data"]["jobId"] = *r.jobId;

                return json_response(r.http_status, out);
            });

    CROW_ROUTE(app_, "/start")
        .methods(crow::HTTPMethod::POST)(
            [this](const crow::request& req)
            {
                int timeout_ms = default_timeout_ms_;

                if (!req.body.empty())
                {
                    auto body = crow::json::load(req.body);
                    if (!body)
                    {
                        crow::json::wvalue out;
                        out["ok"] = false;
                        out["message"] = "ERR:BAD_REQUEST";
                        return json_response(400, out);
                    }

                    if (body.t() != crow::json::type::Object)
                    {
                        crow::json::wvalue out;
                        out["ok"] = false;
                        out["message"] = "ERR:BAD_REQUEST";
                        return json_response(400, out);
                    }

                    if (body.has("timeout_ms"))
                    {
                        if (body["timeout_ms"].t() != crow::json::type::Number)
                        {
                            crow::json::wvalue out;
                            out["ok"] = false;
                            out["message"] = "ERR:BAD_REQUEST";
                            return json_response(400, out);
                        }
                        timeout_ms = static_cast<int>(body["timeout_ms"].i());
                        if (timeout_ms <= 0)
                        {
                            crow::json::wvalue out;
                            out["ok"] = false;
                            out["message"] = "ERR:BAD_REQUEST";
                            return json_response(400, out);
                        }
                    }
                }

                auto fut = dispatcher_.submit_start_job(timeout_ms);
                if (fut.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
                {
                    crow::json::wvalue out;
                    out["ok"] = false;
                    out["message"] = "ERR:BUSY";
                    return json_response(503, out);
                }

                const core::Response r = fut.get();
                if (!r.ok)
                {
                    crow::json::wvalue out;
                    out["ok"] = false;
                    out["message"] = r.message;
                    return json_response(r.http_status, out);
                }

                const std::string jobId = jobs_.start(timeout_ms);

                crow::json::wvalue out;
                out["ok"] = true;
                out["jobId"] = jobId;
                out["state"] = core::to_string(core_.state());
                out["message"] = "ACCEPTED";
                return json_response(202, out);
            });

    CROW_ROUTE(app_, "/result/<string>")
        .methods(crow::HTTPMethod::GET)(
            [this](const std::string& id)
            {
                if (!store_.exists(id))
                {
                    crow::json::wvalue out;
                    out["ok"] = false;
                    out["message"] = "ERR:NOT_FOUND";
                    return json_response(404, out);
                }

                const auto res = store_.get(id);

                crow::json::wvalue out;
                out["ok"] = true;
                out["jobId"] = id;
                out["status"] = job_status_to_string(res.status);
                out["state"] = core::to_string(core_.state());
                out["message"] = res.message;

                ensure_data_object(out);
                if (res.qr.has_value())
                    out["data"]["qr"] = *res.qr;

                return json_response(200, out);
            });
}

/**
 * @brief Runs the Crow HTTP server.
 *
 * The server binds to the configured address/port and runs in multithreaded mode.
 * This call blocks until the server is stopped.
 */
void RestServer::run()
{
    app_.bindaddr(bind_addr_).port(port_).multithreaded().run();
}

} // namespace adapters::rest
