#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "app/Dispatcher.hpp"
#include "core/Core.hpp"

using namespace std::chrono_literals;

TEST_CASE("Dispatcher: submit_sync executes on worker thread", "[dispatcher]")
{
    core::Core c;
    app::Dispatcher d(c);

    auto fut = d.submit_sync("PING");
    REQUIRE(fut.wait_for(1s) == std::future_status::ready);

    const auto r = fut.get();
    REQUIRE(r.ok == true);
    REQUIRE(r.command == "PING");
    REQUIRE(r.message == "PONG");

    d.stop();
}

TEST_CASE("Dispatcher: sequential commands produce deterministic core transitions", "[dispatcher]")
{
    core::Core c;
    app::Dispatcher d(c);

    auto f1 = d.submit_sync("INIT");
    REQUIRE(f1.wait_for(1s) == std::future_status::ready);
    REQUIRE(f1.get().state == core::State::INIT);

    auto f2 = d.submit_start_job(200);
    REQUIRE(f2.wait_for(1s) == std::future_status::ready);

    const auto r2 = f2.get();
    REQUIRE(r2.ok == true);
    REQUIRE(r2.http_status == 202);
    REQUIRE(r2.state == core::State::RUNNING);

    auto f3 = d.submit_stop();
    REQUIRE(f3.wait_for(1s) == std::future_status::ready);
    REQUIRE(f3.get().state == core::State::STOPPED);

    d.stop();
}
