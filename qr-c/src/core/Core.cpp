/**
 * @file Core.cpp
 * @brief Implementation of core::Core command handling and simple state transitions.
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
 */

#include "Core.hpp"

#include <cctype>

namespace core
{

/**
 * @brief Converts a string to uppercase using C locale semantics.
 *
 * The conversion uses std::toupper and casts through unsigned char to avoid
 * undefined behavior for negative char values.
 *
 * @param s Input string.
 * @return std::string Uppercased copy of @p s.
 */
std::string Core::upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

/**
 * @brief Handles a synchronous command and returns a Response.
 *
 * Supported commands (case-insensitive):
 * - PING: returns message "PONG"
 * - INIT: transitions state to INIT and returns "OK"
 * - STOP: transitions state to STOPPED via stop()
 * - START: rejected (400) with "ERR:USE_START_ENDPOINT" (prefer async flow)
 *
 * Unknown commands return 400 "ERR:UNKNOWN_CMD".
 *
 * @param cmd Command string to execute.
 * @return Response Result of command execution including state and message.
 */
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
        // Optional: synchronous START is intentionally not supported here.
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

/**
 * @brief Starts an asynchronous job by transitioning the state machine.
 *
 * This method is intended to be called by an async orchestration layer
 * (e.g., Dispatcher + REST /start), not directly as a synchronous command.
 *
 * Behavior:
 * - If the core is not initialized (State::NOT_INIT), returns 409 "ERR:NOT_INIT".
 * - Otherwise transitions to State::RUNNING and returns 202 "ACCEPTED".
 *
 * @param timeout_ms Job timeout in milliseconds (currently unused by Core).
 * @return Response Result of the job-start attempt.
 */
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

    st_ = State::RUNNING;
    r.state = st_;
    r.message = "ACCEPTED";
    r.http_status = 202;
    return r;
}

/**
 * @brief Stops the core and transitions to State::STOPPED.
 *
 * @return Response Result of the stop operation.
 */
Response Core::stop()
{
    Response r;
    r.command = "STOP";
    st_ = State::STOPPED;
    r.state = st_;
    r.message = "OK";
    return r;
}

} // namespace core
