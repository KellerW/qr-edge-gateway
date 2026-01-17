/* SPDX-License-Identifier: CC-BY-NC-4.0 */
/**
 * @file Core.hpp
 * @brief Declares core::Core, a minimal stateful command handler and job-start interface.
 *
 * The Core class represents the application's domain "engine" / state machine.
 * It exposes:
 * - synchronous command handling (e.g., PING/INIT/STOP),
 * - a dedicated entry point to transition into a job-running state (start_job),
 * - a stop operation (stop),
 * - state inspection (state()).
 *
 * The data structures returned by Core are defined in Types.hpp:
 * - core::Response (includes ok/http_status/command/state/message and optional payload)
 * - core::State
 */

#pragma once
#include "Types.hpp"

#include <string>

namespace core
{

/**
 * @class Core
 * @brief Core domain state machine and command execution entry point.
 *
 * Design intent:
 * - Keep domain logic here (state transitions and validation).
 * - Let adapters (REST) and application services (Dispatcher/JobRunner) orchestrate
 *   execution and asynchronous behavior.
 */
class Core
{
  public:
    /**
     * @brief Handles a synchronous command.
     *
     * Expected commands are case-insensitive and typically include:
     * - PING
     * - INIT
     * - STOP
     * - START (optionally rejected to force async /start usage)
     *
     * @param cmd Command string to execute.
     * @return Response Result of command execution, including current/new state.
     */
    Response handle_sync_command(const std::string& cmd); // PING/INIT/STOP (+ optional START)

    /**
     * @brief Starts an asynchronous job by transitioning Core state.
     *
     * This method is intended to be triggered by a higher-level orchestration layer
     * (e.g., Dispatcher + REST /start). The job execution itself is handled elsewhere.
     *
     * @param timeout_ms Job timeout in milliseconds (may be used for validation/policy).
     * @return Response Result of the start request (e.g., 202 ACCEPTED or 409 NOT_INIT).
     */
    Response start_job(int timeout_ms); // transitions to RUNNING when allowed

    /**
     * @brief Stops the Core and transitions to STOPPED.
     *
     * @return Response Result of the stop operation.
     */
    Response stop(); // STOP -> STOPPED

    /**
     * @brief Returns the current Core state.
     *
     * @return State Current state.
     */
    State state() const { return st_; }

    Response finish_job(); // RUNNING -> INIT (when allowed)

  private:
    /**
     * @brief Converts a string to uppercase.
     *
     * Used to normalize commands so they can be processed case-insensitively.
     *
     * @param s Input string.
     * @return std::string Uppercased copy of @p s.
     */
    static std::string upper(std::string s);

  private:
    /// Current state of the core state machine.
    State st_{State::NOT_INIT};
};

} // namespace core
