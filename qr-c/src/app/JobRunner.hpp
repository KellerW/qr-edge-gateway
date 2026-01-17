/* SPDX-License-Identifier: CC-BY-NC-4.0 */
#pragma once

#include "JobStore.hpp"
#include "Dispatcher.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace app
{

class JobRunner
{
  public:
    JobRunner(JobStore& store, Dispatcher& dispatcher, std::string serial_port)
        : store_(store), dispatcher_(dispatcher), serial_port_(std::move(serial_port))
    {
    }

    ~JobRunner() { stop(); }

    std::string start(int timeout_ms);
    void stop();

    // Option B: allow INIT to configure baudrate
    void set_baudrate(int baudrate);

  private:
    void run_job(std::string id, int timeout_ms);

  private:
    JobStore& store_;
    Dispatcher& dispatcher_;
    std::string serial_port_;

    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<int> baudrate_{115200};
};

} // namespace app
