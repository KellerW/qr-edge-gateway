/* SPDX-License-Identifier: CC-BY-NC-4.0 */
/**
 * @file Core.cpp
 * @brief Implementation of core::Core command handling and guarded state transitions.
 *
 * The Core component encapsulates the application state machine and provides:
 * - basic command normalization (upper-casing),
 * - synchronous command handling (PING/INIT/STOP/START),
 * - job-start transition (start_job),
 * - stop transition (stop).
 *
 * Notes on semantics:
 * - Commands are case-insensitive due to normalization (upper()).
 * - START is intentionally rejected in handle_sync_command(); clients should use
 *   the asynchronous start endpoint (e.g., REST /start), which calls start_job().
 *
 * Guarded lifecycle:
 * - NOT_INIT -> INIT -> RUNNING -> STOPPED
 * - INIT is also allowed from STOPPED (re-init).
 * - INIT is rejected while RUNNING (409 ERR:BUSY).
 * - start_job is allowed only from INIT:
 *     - NOT_INIT  -> 409 ERR:NOT_INIT
 *     - RUNNING   -> 409 ERR:ALREADY_RUNNING
 *     - STOPPED   -> 409 ERR:STOPPED
 */

#include "Core.hpp"

#include <cctype>

namespace core
{

std::string Core::upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Response Core::handle_sync_command(const std::string& cmd)
{
    Response r;
    r.command = upper(cmd);
    r.state = st_;

    if (r.command == "PING")
    {
        r.message = "PONG";
        return r;
    }

    if (r.command == "INIT")
    {
        // Do not allow re-initialization while RUNNING.
        if (st_ == State::RUNNING)
        {
            r.ok = false;
            r.http_status = 409;
            r.message = "ERR:BUSY";
            return r;
        }

        // Allow INIT from NOT_INIT or STOPPED; idempotent if already INIT.
        st_ = State::INIT;
        r.state = st_;
        r.message = "OK";
        return r;
    }

    if (r.command == "STOP")
    {
        return stop();
    }

    if (r.command == "START")
    {
        // Synchronous START is intentionally not supported here.
        r.ok = false;
        r.http_status = 400;
        r.message = "ERR:USE_START_ENDPOINT";
        return r;
    }

    r.ok = false;
    r.http_status = 400;
    r.message = "ERR:UNKNOWN_CMD";
    return r;
}

Response Core::start_job(int /*timeout_ms*/)
{
    Response r;
    r.command = "START";
    r.state = st_;

    if (st_ == State::NOT_INIT)
    {
        r.ok = false;
        r.http_status = 409;
        r.message = "ERR:NOT_INIT";
        return r;
    }

    if (st_ == State::RUNNING)
    {
        r.ok = false;
        r.http_status = 409;
        r.message = "ERR:ALREADY_RUNNING";
        return r;
    }

    if (st_ == State::STOPPED)
    {
        r.ok = false;
        r.http_status = 409;
        r.message = "ERR:STOPPED";
        return r;
    }

    // Only INIT reaches here.
    st_ = State::RUNNING;
    r.state = st_;
    r.message = "ACCEPTED";
    r.http_status = 202;
    return r;
}

Response Core::stop()
{
    Response r;
    r.command = "STOP";
    st_ = State::STOPPED;
    r.state = st_;
    r.message = "OK";
    return r;
}

Response Core::finish_job()
{
    Response r;
    r.command = "FINISH";
    r.state = st_;

    if (st_ == State::NOT_INIT)
    {
        r.ok = false;
        r.http_status = 409;
        r.message = "ERR:NOT_INIT";
        return r;
    }
    if (st_ == State::STOPPED)
    {
        r.ok = false;
        r.http_status = 409;
        r.message = "ERR:STOPPED";
        return r;
    }
    if (st_ == State::RUNNING)
    {
        st_ = State::INIT;
    }

    r.state = st_;
    r.message = "OK";
    return r;
}


} // namespace core
