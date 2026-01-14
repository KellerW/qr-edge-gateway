/**
 * @file JobStore.hpp
 * @brief In-memory store for asynchronous job lifecycle and results.
 *
 * JobStore tracks jobs by a string identifier and provides thread-safe operations to:
 * - create a job entry (initially PENDING),
 * - check existence,
 * - retrieve a snapshot of the current JobResult,
 * - update a job to DONE (with optional QR payload),
 * - update a job to TIMEOUT (clearing any payload).
 *
 * Concurrency:
 * - All public methods are thread-safe.
 * - Access is serialized via an internal mutex.
 *
 * Notes:
 * - This is an in-memory store; data is lost when the process terminates.
 * - Retrieval returns a copy (snapshot) of the stored JobResult.
 */

#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace app
{

/**
 * @enum JobStatus
 * @brief Represents the lifecycle state of a job.
 */
enum class JobStatus
{
    /// Job has been created but is not yet completed.
    PENDING,

    /// Job completed successfully.
    DONE,

    /// Job exceeded its allowed time budget.
    TIMEOUT
};

/**
 * @struct JobResult
 * @brief Job status and payload stored in JobStore.
 *
 * The QR payload is optional and may be absent (e.g., on TIMEOUT).
 */
struct JobResult
{
    /// Current job status.
    JobStatus status{JobStatus::PENDING};

    /// Human-readable status message (e.g., "PENDING", "OK", "TIMEOUT").
    std::string message{"PENDING"};

    /// Optional QR payload produced by the job when available.
    std::optional<std::string> qr;
};

/**
 * @class JobStore
 * @brief Thread-safe in-memory map of job id -> JobResult.
 *
 * JobStore is designed to be used by:
 * - a producer (e.g., JobRunner) updating job completion,
 * - consumers (e.g., REST endpoints) polling for job status/results.
 */
class JobStore
{
  public:
    /**
     * @brief Creates a new job entry with status PENDING.
     *
     * If an entry already exists for the same id, it will be overwritten with a
     * fresh PENDING result.
     *
     * @param id Job identifier.
     */
    void create(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(m_);
        jobs_[id] = JobResult{};
    }

    /**
     * @brief Checks whether a job exists.
     *
     * @param id Job identifier.
     * @return true if the job exists, false otherwise.
     */
    bool exists(const std::string& id) const
    {
        std::lock_guard<std::mutex> lk(m_);
        return jobs_.find(id) != jobs_.end();
    }

    /**
     * @brief Retrieves a snapshot of a job result.
     *
     * @param id Job identifier.
     * @return JobResult Copy of the current job result.
     *
     * @throws std::out_of_range if @p id does not exist.
     */
    JobResult get(const std::string& id) const
    {
        std::lock_guard<std::mutex> lk(m_);
        return jobs_.at(id);
    }

    /**
     * @brief Marks a job as DONE and stores the QR payload.
     *
     * @param id Job identifier.
     * @param qr QR payload to store.
     *
     * @throws std::out_of_range if @p id does not exist.
     */
    void set_done(const std::string& id, const std::string& qr)
    {
        std::lock_guard<std::mutex> lk(m_);
        auto& r = jobs_.at(id);
        r.status = JobStatus::DONE;
        r.message = "OK";
        r.qr = qr;
    }

    /**
     * @brief Marks a job as TIMEOUT and clears any existing payload.
     *
     * @param id Job identifier.
     *
     * @throws std::out_of_range if @p id does not exist.
     */
    void set_timeout(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(m_);
        auto& r = jobs_.at(id);
        r.status = JobStatus::TIMEOUT;
        r.message = "TIMEOUT";
        r.qr.reset();
    }

  private:
    /// Mutex protecting the jobs map.
    mutable std::mutex m_;

    /// In-memory job results keyed by job id.
    std::unordered_map<std::string, JobResult> jobs_;
};

} // namespace app
