/**
 * @file JobRunner.hpp
 * @brief Declares app::JobRunner, a single-job background runner that updates JobStore.
 *
 * JobRunner manages at most one active job at a time:
 * - start() stops any existing job, creates a new job entry in JobStore, and
 *   spawns a worker thread to execute the job logic.
 * - stop() requests cooperative cancellation and joins the worker thread.
 *
 * The job outcome (DONE/TIMEOUT and any payload) is published to JobStore so it can
 * be polled by other layers (e.g., REST API).
 */

#pragma once
#include "JobStore.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace app
{

/**
 * @class JobRunner
 * @brief Runs a single background job and publishes results to JobStore.
 *
 * Concurrency model:
 * - Only one job can be active at a time.
 * - A worker thread executes the job and updates JobStore upon completion.
 * - Cancellation is cooperative using an atomic running_ flag.
 *
 * Lifetime expectations:
 * - The referenced JobStore must outlive the JobRunner instance.
 */
class JobRunner
{
  public:
    /**
     * @brief Constructs a JobRunner bound to a JobStore.
     *
     * @param store Reference to the JobStore used to create and update job entries.
     */
    explicit JobRunner(JobStore& store) : store_(store) {}

    /**
     * @brief Destructor; stops any running job and joins the worker thread.
     */
    ~JobRunner() { stop(); }

    /**
     * @brief Starts a new job with the given timeout.
     *
     * Semantics:
     * - If a job is currently active, it is stopped first (single-job policy).
     * - A new job id is generated and a new entry is created in JobStore.
     * - A worker thread is launched to execute the job logic.
     *
     * @param timeout_ms Job timeout in milliseconds.
     * @return std::string Job identifier that can be used to poll results.
     *
     * @note This API enforces "1 job at a time" by design.
     */
    std::string start(int timeout_ms);

    /**
     * @brief Stops the currently running job (if any) and joins the worker thread.
     *
     * This method is safe to call multiple times.
     * Cancellation is cooperative (the worker checks the running_ flag).
     */
    void stop();

  private:
    /**
     * @brief Worker-thread function implementing the job logic.
     *
     * The implementation is expected to:
     * - honor @p timeout_ms semantics,
     * - update JobStore with DONE/TIMEOUT and any payload,
     * - exit promptly when running_ becomes false.
     *
     * @param id         Job identifier for the store entry to update.
     * @param timeout_ms Job timeout in milliseconds.
     */
    void run_job(std::string id, int timeout_ms);

  private:
    /// JobStore used for job lifecycle tracking and publishing results.
    JobStore& store_;

    /// Cooperative cancellation flag for the worker thread.
    std::atomic<bool> running_{false};

    /// Worker thread running the job logic.
    std::thread worker_;
};

} // namespace app
