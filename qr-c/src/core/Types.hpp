/**
 * @file Types.hpp
 * @brief Core domain types: state machine states and standardized command responses.
 *
 * This header defines:
 * - core::State: the core state machine states.
 * - core::to_string(State): stable string conversion used for logs/REST payloads.
 * - core::Response: a normalized response object used across layers (Core/Dispatcher/REST).
 *
 * Conventions:
 * - Response::http_status contains the HTTP status code that adapters may return.
 * - Response::ok indicates logical success/failure (independent of HTTP code).
 * - Optional fields (jobId/qr) are set when applicable.
 */

#pragma once
#include <optional>
#include <string>

namespace core
{

/**
 * @enum State
 * @brief Represents the core state machine states.
 */
enum class State
{
    /// Initial state before INIT.
    NOT_INIT,

    /// Core has been initialized and is ready to accept work.
    INIT,

    /// Core is currently running a job.
    RUNNING,

    /// Core has been stopped.
    STOPPED
};

/**
 * @brief Converts a core::State into a stable string representation.
 *
 * This helper is used for:
 * - REST API responses,
 * - logging / debugging,
 * - any UI/display layer needing stable labels.
 *
 * @param s State value.
 * @return std::string Uppercase stable string (e.g., "NOT_INIT"), or "UNKNOWN" if not recognized.
 */
inline std::string to_string(State s)
{
    switch (s)
    {
    case State::NOT_INIT:
        return "NOT_INIT";
    case State::INIT:
        return "INIT";
    case State::RUNNING:
        return "RUNNING";
    case State::STOPPED:
        return "STOPPED";
    }
    return "UNKNOWN";
}

/**
 * @struct Response
 * @brief Standardized result returned by core operations and propagated to adapters.
 *
 * Response is intentionally aligned with typical HTTP/REST usage:
 * - http_status defines the HTTP status code to use (default: 200).
 * - ok signals logical success/failure.
 * - command identifies the executed command or operation label.
 * - state reflects the state machine snapshot at the time of response creation.
 * - message provides a stable, machine-friendly message (e.g., "OK", "ERR:NOT_INIT").
 * - jobId/qr are optional payload fields set when applicable.
 */
struct Response
{
    /// HTTP status code suggested for the REST layer (default: 200).
    int http_status{200};

    /// Logical success flag (default: true).
    bool ok{true};

    /// Command/operation label associated with this response.
    std::string command;

    /// Core state snapshot for this response.
    State state{State::NOT_INIT};

    /// Message describing the outcome (e.g., "OK", "ERR:UNKNOWN_CMD").
    std::string message;

    /// Optional job identifier returned by operations that create/track jobs.
    std::optional<std::string> jobId;

    /// Optional QR payload returned by operations that produce QR data.
    std::optional<std::string> qr;
};

} // namespace core
