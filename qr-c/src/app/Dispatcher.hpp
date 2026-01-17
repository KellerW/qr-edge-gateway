/* SPDX-License-Identifier: CC-BY-NC-4.0 */
/**
 * @file Dispatcher.hpp
 * @brief Single-worker task dispatcher that serializes access to core::Core.
 *
 * This component provides an asynchronous submission interface (std::future-based)
 * for executing Core operations on a dedicated worker thread. It is primarily used
 * to:
 * - execute synchronous commands (SYNC_COMMAND),
 * - initiate jobs with timeouts (START_JOB),
 * - request a stop operation (STOP).
 *
 * Concurrency model:
 * - A single worker thread consumes Tasks from an internal queue.
 * - Callers submit work and receive a std::future<core::Response>.
 * - core::Core is accessed only by the worker thread, avoiding concurrent calls.
 *
 * Thread-safety:
 * - submit_* methods are thread-safe and may be called concurrently.
 * - stop() is idempotent and thread-safe.
 *
 * Lifetime:
 * - Dispatcher starts its worker thread upon construction.
 * - Dispatcher stops and joins the worker thread in the destructor.
 */

#pragma once
#include "../core/Core.hpp"

#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

namespace app
{

/**
 * @struct Task
 * @brief Work item consumed by Dispatcher::loop().
 *
 * A Task carries:
 * - a Kind describing what to execute,
 * - optional arguments (command/timeout),
 * - a promise used to deliver the core::Response back to the submitter.
 */
struct Task
{
    /**
     * @enum Kind
     * @brief Type of work represented by a Task.
     */
    enum class Kind
    {
        /// Execute a synchronous command string via core::Core::handle_sync_command().
        SYNC_COMMAND,

        /// Start a job with timeout via core::Core::start_job().
        START_JOB,

        /// Request a stop via core::Core::stop().
        STOP,

        FINISH_JOB

    };

    /// Task kind (defaults to SYNC_COMMAND).
    Kind kind{Kind::SYNC_COMMAND};

    /// Command payload used when kind == SYNC_COMMAND.
    std::string command;

    /// Timeout in milliseconds used when kind == START_JOB.
    int timeout_ms{0};

    /// Promise that will be fulfilled with the resulting core::Response.
    std::promise<core::Response> prom;
};

/**
 * @class Dispatcher
 * @brief Serializes Core operations onto a dedicated worker thread.
 *
 * Dispatcher decouples request submission from execution:
 * - Submission is non-blocking (returns a future immediately).
 * - Execution happens sequentially on a single background thread.
 *
 * Error handling:
 * - Any exception thrown while handling a Task is caught and converted into an
 *   error core::Response with HTTP status 500 and message "ERR:INTERNAL".
 *
 * @note The underlying core::Core reference must outlive the Dispatcher.
 */
class Dispatcher
{
  public:
    /**
     * @brief Constructs a Dispatcher and starts the worker thread.
     *
     * @param core Reference to the core::Core instance that will be invoked by the worker.
     */
    explicit Dispatcher(core::Core& core) : core_(core), worker_(&Dispatcher::loop, this) {}

    /**
     * @brief Destructor; stops and joins the worker thread.
     *
     * Equivalent to calling stop().
     */
    ~Dispatcher() { stop(); }

    /**
     * @brief Submits a synchronous command for execution.
     *
     * The command will be executed by the worker thread via core::Core::handle_sync_command().
     *
     * @param cmd Command string.
     * @return std::future<core::Response> Future that becomes ready once execution completes.
     */
    std::future<core::Response> submit_sync(std::string cmd)
    {
        Task t;
        t.kind = Task::Kind::SYNC_COMMAND;
        t.command = std::move(cmd);
        auto fut = t.prom.get_future();
        push(std::move(t));
        return fut;
    }

    /**
     * @brief Submits a job-start request for execution.
     *
     * The request will be executed by the worker thread via core::Core::start_job(timeout_ms).
     *
     * @param timeout_ms Job timeout in milliseconds.
     * @return std::future<core::Response> Future that becomes ready once execution completes.
     */
    std::future<core::Response> submit_start_job(int timeout_ms)
    {
        Task t;
        t.kind = Task::Kind::START_JOB;
        t.timeout_ms = timeout_ms;
        auto fut = t.prom.get_future();
        push(std::move(t));
        return fut;
    }

    /**
     * @brief Submits a stop request for execution.
     *
     * The request will be executed by the worker thread via core::Core::stop().
     *
     * @return std::future<core::Response> Future that becomes ready once stop completes.
     */
    std::future<core::Response> submit_stop()
    {
        Task t;
        t.kind = Task::Kind::STOP;
        auto fut = t.prom.get_future();
        push(std::move(t));
        return fut;
    }

    std::future<core::Response> submit_finish_job()
    {
        Task t;
        t.kind = Task::Kind::FINISH_JOB;
        auto fut = t.prom.get_future();
        push(std::move(t));
        return fut;
    }

    /**
     * @brief Stops the dispatcher and joins the worker thread.
     *
     * This method is:
     * - thread-safe,
     * - idempotent (calling multiple times is safe),
     * - blocking until the worker thread terminates.
     *
     * Stop semantics:
     * - Sets an internal stopping flag.
     * - Wakes the worker.
     * - Worker exits once the queue is drained (or immediately if empty).
     */
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (stopping_)
                return;
            stopping_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

  private:
    /**
     * @brief Enqueues a Task and wakes the worker thread.
     *
     * @param t Task to enqueue (moved into the internal queue).
     */
    void push(Task&& t)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push(std::move(t));
        }
        cv_.notify_one();
    }

    /**
     * @brief Worker thread loop.
     *
     * Waits for tasks to be available or for stop() to be requested. Executes tasks
     * sequentially and fulfills each Task promise with a core::Response.
     */
    void loop()
    {
        for (;;)
        {
            Task t;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stopping_ || !q_.empty(); });
                if (stopping_ && q_.empty())
                    return;
                t = std::move(q_.front());
                q_.pop();
            }

            core::Response r;
            try
            {
                if (t.kind == Task::Kind::SYNC_COMMAND)
                {
                    r = core_.handle_sync_command(t.command);
                }
                else if (t.kind == Task::Kind::START_JOB)
                {
                    r = core_.start_job(t.timeout_ms);
                }
                else if (t.kind == Task::Kind::FINISH_JOB)
                {
                    r = core_.finish_job();
                }
                else
                {
                    r = core_.stop();
                }
            }
            catch (...)
            {
                r.ok = false;
                r.http_status = 500;
                r.command = "INTERNAL";
                r.state = core_.state();
                r.message = "ERR:INTERNAL";
            }
            t.prom.set_value(std::move(r));
        }
    }

  private:
    /// Reference to the core domain object executed by the worker thread.
    core::Core& core_;

    /// Mutex protecting the queue and the stopping flag.
    std::mutex m_;

    /// Condition variable used to wake the worker on new tasks or stop requests.
    std::condition_variable cv_;

    /// FIFO queue of pending tasks.
    std::queue<Task> q_;

    /// Stop flag; when true, worker exits after draining the queue.
    bool stopping_{false};

    /// Worker thread that consumes tasks.
    std::thread worker_;
};

} // namespace app
