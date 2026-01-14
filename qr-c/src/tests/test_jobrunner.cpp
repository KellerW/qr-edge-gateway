#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <thread>

#include "app/JobRunner.hpp"
#include "app/JobStore.hpp"

using namespace std::chrono_literals;

static bool wait_until(std::function<bool()> pred, std::chrono::milliseconds timeout, std::chrono::milliseconds poll)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(poll);
    }
    return pred();
}

TEST_CASE("JobRunner: start creates job entry in store", "[jobrunner]")
{
    app::JobStore store;
    app::JobRunner runner(store);

    const std::string id = runner.start(200);
    REQUIRE(id.size() == 8);
    REQUIRE(store.exists(id) == true);

    runner.stop();
}

TEST_CASE("JobRunner: stop cancels job (can remain PENDING)", "[jobrunner]")
{
    app::JobStore store;
    app::JobRunner runner(store);

    const std::string id = runner.start(200);
    runner.stop();

    // Cooperative cancellation: if stopped early, the worker may exit without updating the store.
    const auto r = store.get(id);
    REQUIRE(r.status == app::JobStatus::PENDING);
    REQUIRE(r.message == "PENDING");
}

TEST_CASE("JobRunner: job eventually completes with DONE in normal timing", "[jobrunner]")
{
    app::JobStore store;
    app::JobRunner runner(store);

    const std::string id = runner.start(400);

    const bool ok = wait_until(
        [&] {
            const auto r = store.get(id);
            return (r.status == app::JobStatus::DONE) || (r.status == app::JobStatus::TIMEOUT);
        },
        2s,
        20ms);

    REQUIRE(ok == true);

    const auto r = store.get(id);
    // With the current simulation (sleep timeout/2), DONE is the expected path.
    REQUIRE(r.status == app::JobStatus::DONE);
    REQUIRE(r.message == "OK");
    REQUIRE(r.qr.has_value() == true);

    runner.stop();
}
