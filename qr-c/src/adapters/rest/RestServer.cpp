/* SPDX-License-Identifier: CC-BY-NC-4.0 */
/**
 * @file RestServer.cpp
 * @brief REST API adapter implemented with Crow.
 *
 * Endpoints:
 * - GET  /health                -> { ok, message }
 * - GET  /status                -> { ok, state }
 * - POST /command               -> { ok, command, state, message, data:{ qr?, jobId? } }
 * - POST /start                 -> { ok, jobId, state, message } (202 Accepted)
 * - GET  /result/{jobId}        -> { ok, jobId, status, state, message, data:{ qr? } }
 * - POST /stop                  -> { ok, state, message } (cancels running job + stops core)
 */

#include "RestServer.hpp"

#include <chrono>
#include <cctype>
#include <exception>
#include <future>
#include <string>

#include <spdlog/spdlog.h>

namespace adapters::rest
{

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
    case app::JobStatus::CANCELLED:
        return "CANCELLED";
    }
    return "UNKNOWN";
}

static crow::response json_response(int code, const crow::json::wvalue& out)
{
    crow::response res(code, out.dump());
    res.set_header("Content-Type", "application/json");
    return res;
}

static void ensure_data_object(crow::json::wvalue& out)
{
    out["data"] = crow::json::wvalue::empty_object();
}

static crow::response error_response(int code, const std::string& msg)
{
    crow::json::wvalue out;
    out["ok"] = false;
    out["message"] = msg;
    return json_response(code, out);
}

static bool parse_json_object(const crow::request& req, crow::json::rvalue& body, crow::response& err)
{
    body = crow::json::load(req.body);
    if (!body)
    {
        err = error_response(400, "ERR:BAD_REQUEST");
        return false;
    }
    if (body.t() != crow::json::type::Object)
    {
        err = error_response(400, "ERR:BAD_REQUEST");
        return false;
    }
    return true;
}

static std::string to_upper_ascii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

RestServer::RestServer(crow::SimpleApp& app,
                       core::Core& core,
                       app::Dispatcher& dispatcher,
                       app::JobRunner& jobs,
                       app::JobStore& store,
                       std::string bind_addr,
                       int port,
                       int default_timeout_ms)
    : app_(app)
    , core_(core)
    , dispatcher_(dispatcher)
    , jobs_(jobs)
    , store_(store)
    , bind_addr_(std::move(bind_addr))
    , port_(port)
    , default_timeout_ms_(default_timeout_ms)
{
}

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
            crow::json::rvalue body;
            crow::response err;
            if (!parse_json_object(req, body, err))
                return err;

            if (!body.has("command") || body["command"].t() != crow::json::type::String)
                return error_response(400, "ERR:BAD_REQUEST");

            const std::string cmd = body["command"].s();
            if (cmd.empty())
                return error_response(400, "ERR:BAD_REQUEST");

            // If params is present, it must be an object (reject null / array / etc.)
            if (body.has("params") && body["params"].t() != crow::json::type::Object)
                return error_response(400, "ERR:BAD_REQUEST");

            // Helper: parse JSON number as STRICT integer (reject float numbers)
            auto parse_int_field = [&](const crow::json::rvalue& obj,
                                       const char* key,
                                       long long& out,
                                       long long minv,
                                       long long maxv) -> bool
            {
                if (!obj.has(key))
                    return true; // not present => ok

                if (obj[key].t() != crow::json::type::Number)
                    return false;

                // Crow stores numbers as "Number" even for doubles; enforce integer-ness
                const double d = obj[key].d();
                if (!std::isfinite(d))
                    return false;

                if (std::floor(d) != d)
                    return false; // reject non-integer numbers (e.g., 4.6e-201)

                const long long v = static_cast<long long>(d);
                if (v < minv || v > maxv)
                    return false;

                out = v;
                return true;
            };

            // Generic params validation (even if command ignores them).
            // Prevent accepting schema-violating requests during fuzzing.
            int baud_from_params = -1;
            int timeout_from_params = -1;

            if (body.has("params"))
            {
                const auto& p = body["params"];

                long long baud_ll = -1;
                if (!parse_int_field(p, "baudrate", baud_ll, 1LL, 2000000LL))
                    return error_response(400, "ERR:BAD_REQUEST");
                if (baud_ll > 0)
                    baud_from_params = static_cast<int>(baud_ll);

                long long tmo_ll = -1;
                if (!parse_int_field(p, "timeout_ms", tmo_ll, 1LL, 3600000LL))
                    return error_response(400, "ERR:BAD_REQUEST");
                if (tmo_ll > 0)
                    timeout_from_params = static_cast<int>(tmo_ll);
            }

            const std::string cmd_up = to_upper_ascii(cmd);
            spdlog::debug("REST /command received cmd='{}'", cmd_up);

            // Option B: INIT may carry baudrate configuration
            if (cmd_up == "INIT")
            {
                int baud = -1;

                // Accept top-level {"baudrate":115200}
                if (body.has("baudrate"))
                {
                    if (body["baudrate"].t() != crow::json::type::Number)
                        return error_response(400, "ERR:BAD_REQUEST");

                    const double d = body["baudrate"].d();
                    if (!std::isfinite(d) || std::floor(d) != d)
                        return error_response(400, "ERR:BAD_REQUEST");

                    const long long v = static_cast<long long>(d);
                    if (v < 1 || v > 2000000)
                        return error_response(400, "ERR:BAD_REQUEST");

                    baud = static_cast<int>(v);
                }

                // Otherwise, use params.baudrate if present
                if (baud < 0 && baud_from_params > 0)
                    baud = baud_from_params;

                if (baud > 0)
                    jobs_.set_baudrate(baud);
            }

            // Enunciado: STOP deve cancelar caso esteja em espera.
            if (cmd_up == "STOP")
                jobs_.stop();

            auto fut = dispatcher_.submit_sync(cmd_up);
            if (fut.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
                return error_response(503, "ERR:BUSY");

            core::Response r;
            try
            {
                r = fut.get();
            }
            catch (const std::exception& e)
            {
                spdlog::error("Dispatcher submit_sync exception: {}", e.what());
                return error_response(500, "ERR:INTERNAL");
            }
            catch (...)
            {
                spdlog::error("Dispatcher submit_sync unknown exception");
                return error_response(500, "ERR:INTERNAL");
            }

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
                // Require a JSON object (at least "{}"). This avoids accepting
                // empty / malformed bodies during fuzzing.
                crow::json::rvalue body;
                crow::response err;
                if (!parse_json_object(req, body, err))
                    return err;

                int timeout_ms = default_timeout_ms_;

                if (body.has("timeout_ms"))
                {
                    if (body["timeout_ms"].t() != crow::json::type::Number)
                        return error_response(400, "ERR:BAD_REQUEST");

                    timeout_ms = static_cast<int>(body["timeout_ms"].i());
                    if (timeout_ms <= 0)
                        return error_response(400, "ERR:BAD_REQUEST");
                }

                spdlog::debug("REST /start received timeout_ms={}", timeout_ms);

                auto fut = dispatcher_.submit_start_job(timeout_ms);
                if (fut.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
                    return error_response(503, "ERR:BUSY");

                core::Response r;
                try
                {
                    r = fut.get();
                }
                catch (const std::exception& e)
                {
                    spdlog::error("Dispatcher submit_start_job exception: {}", e.what());
                    return error_response(500, "ERR:INTERNAL");
                }
                catch (...)
                {
                    spdlog::error("Dispatcher submit_start_job unknown exception");
                    return error_response(500, "ERR:INTERNAL");
                }

                if (!r.ok)
                {
                    spdlog::debug("REST /start rejected: status={} msg={}", r.http_status, r.message);
                    return error_response(r.http_status, r.message);
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
                spdlog::debug("REST /result id='{}'", id);

                if (!store_.exists(id))
                    return error_response(404, "ERR:NOT_FOUND");

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

    CROW_ROUTE(app_, "/stop")
        .methods(crow::HTTPMethod::POST)(
            [this]()
            {
                // 1) cancel job if running/waiting
                jobs_.stop();

                // 2) stop core (serialized via dispatcher)
                auto fut = dispatcher_.submit_stop();
                if (fut.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
                    return error_response(503, "ERR:BUSY");

                core::Response r;
                try
                {
                    r = fut.get();
                }
                catch (const std::exception& e)
                {
                    spdlog::error("Dispatcher submit_stop exception: {}", e.what());
                    return error_response(500, "ERR:INTERNAL");
                }
                catch (...)
                {
                    spdlog::error("Dispatcher submit_stop unknown exception");
                    return error_response(500, "ERR:INTERNAL");
                }

                crow::json::wvalue out;
                out["ok"] = r.ok;
                out["state"] = core::to_string(r.state);
                out["message"] = r.message;
                return json_response(r.http_status, out);
            });
}

void RestServer::run()
{
    spdlog::info("REST server binding to {}:{} (default_timeout_ms={})", bind_addr_, port_, default_timeout_ms_);
    app_.bindaddr(bind_addr_).port(port_).multithreaded().run();
}

} // namespace adapters::rest
