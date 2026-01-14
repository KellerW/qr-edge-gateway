/**
 * @file JobRunner.cpp
 * @brief Background job execution helper for producing and storing job results.
 *
 * JobRunner is responsible for:
 * - generating a job id,
 * - creating a job entry in app::JobStore,
 * - running a background thread that completes the job (DONE/TIMEOUT),
 * - stopping and joining any previous job thread before starting a new one.
 *
 * Current behavior:
 * - The job implementation is a placeholder/simulation (sleep + TIMEOUT/DONE decision).
 * - In a real system, run_job() would typically block on I/O (e.g., serial read),
 *   and then write the final result to the store.
 */

#include "JobRunner.hpp"

#include <chrono>
#include <random>

namespace app
{

/**
 * @brief Generates a short hexadecimal job identifier.
 *
 * The current implementation uses std::random_device to generate 8 hex characters.
 * This is sufficient for a lightweight in-memory job id but is not intended to be
 * cryptographically strong or globally unique across processes.
 *
 * @return std::string 8-character hex job id (lowercase).
 */
static std::string gen_id()
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i)
        s.push_back(hex[rd() % 16]);
    return s;
}

/**
 * @brief Starts a new background job and returns its job id.
 *
 * Semantics:
 * - Stops any currently running job (if present) by calling stop().
 * - Creates a new job entry in the JobStore using a newly generated id.
 * - Spawns a worker thread that runs JobRunner::run_job().
 *
 * @param timeout_ms Timeout in milliseconds used by the job logic.
 * @return std::string Job identifier for later polling via JobStore/REST.
 *
 * @note The current implementation always stops the previous job before starting
 *       a new one. If you need true concurrency, JobRunner must be redesigned.
 */
std::string JobRunner::start(int timeout_ms)
{
    // If a job is already active, stop it before starting a new one.
    stop();

    const std::string id = gen_id();
    store_.create(id);

    running_ = true;
    worker_ = std::thread(&JobRunner::run_job, this, id, timeout_ms);
    return id;
}

/**
 * @brief Job worker function executed on a dedicated thread.
 *
 * Current placeholder logic:
 * - Computes a deadline (now + timeout).
 * - Sleeps for half the timeout (simulating work / I/O wait).
 * - If still running:
 *   - marks TIMEOUT if the deadline has passed,
 *   - otherwise marks DONE with a dummy QR payload.
 *
 * @param id         Job identifier associated with the store entry.
 * @param timeout_ms Timeout in milliseconds for the simulated job.
 */
void JobRunner::run_job(std::string id, int timeout_ms)
{
    // Replace this simulation with real I/O (e.g., wait for serial data).
    using namespace std::chrono;

    const auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    std::this_thread::sleep_for(milliseconds(timeout_ms / 2));

    if (!running_)
        return;

    if (steady_clock::now() >= deadline)
    {
        store_.set_timeout(id);
    }
    else
    {
        store_.set_done(id, "QR:123456");
    }
    running_ = false;
}

/**
 * @brief Stops the currently running job (if any) and joins the worker thread.
 *
 * This method:
 * - clears the running flag (cooperative cancellation),
 * - joins the worker thread if it is joinable.
 *
 * @note Cancellation is cooperative; the worker must check running_ to exit early.
 */
void JobRunner::stop()
{
    running_ = false;
    if (worker_.joinable())
        worker_.join();
}

} // namespace app
