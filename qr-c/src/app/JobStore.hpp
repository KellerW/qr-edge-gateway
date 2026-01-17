/* SPDX-License-Identifier: CC-BY-NC-4.0 */
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace app
{

enum class JobStatus
{
    PENDING,
    DONE,
    TIMEOUT,
    CANCELLED
};

struct JobResult
{
    JobStatus status{JobStatus::PENDING};
    std::string message{"PENDING"};
    std::optional<std::string> qr;
};

class JobStore
{
  public:
    void create(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(m_);
        jobs_[id] = JobResult{};
    }

    bool exists(const std::string& id) const
    {
        std::lock_guard<std::mutex> lk(m_);
        return jobs_.find(id) != jobs_.end();
    }

    JobResult get(const std::string& id) const
    {
        std::lock_guard<std::mutex> lk(m_);
        return jobs_.at(id);
    }

    void set_done(const std::string& id, const std::string& qr)
    {
        std::lock_guard<std::mutex> lk(m_);
        auto& r = jobs_.at(id);
        r.status = JobStatus::DONE;
        r.message = "OK";
        r.qr = qr;
    }

    void set_timeout(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(m_);
        auto& r = jobs_.at(id);
        r.status = JobStatus::TIMEOUT;
        r.message = "TIMEOUT";
        r.qr.reset();
    }

    void set_cancelled(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(m_);
        auto& r = jobs_.at(id);
        r.status = JobStatus::CANCELLED;
        r.message = "CANCELLED";
        r.qr.reset();
    }

  private:
    mutable std::mutex m_;
    std::unordered_map<std::string, JobResult> jobs_;
};

} // namespace app
