#include <catch2/catch_test_macros.hpp>

#include "core/Core.hpp"

TEST_CASE("Core: initial state and PING", "[core]")
{
    core::Core c;

    REQUIRE(c.state() == core::State::NOT_INIT);

    const auto r = c.handle_sync_command("ping");
    REQUIRE(r.ok == true);
    REQUIRE(r.http_status == 200);
    REQUIRE(r.command == "PING");
    REQUIRE(r.message == "PONG");
    REQUIRE(r.state == core::State::NOT_INIT);
}

TEST_CASE("Core: INIT transitions state to INIT", "[core]")
{
    core::Core c;

    const auto r = c.handle_sync_command("INIT");
    REQUIRE(r.ok == true);
    REQUIRE(r.http_status == 200);
    REQUIRE(r.command == "INIT");
    REQUIRE(r.message == "OK");
    REQUIRE(r.state == core::State::INIT);
    REQUIRE(c.state() == core::State::INIT);
}

TEST_CASE("Core: START via sync command is rejected", "[core]")
{
    core::Core c;

    const auto r = c.handle_sync_command("START");
    REQUIRE(r.ok == false);
    REQUIRE(r.http_status == 400);
    REQUIRE(r.command == "START");
    REQUIRE(r.message == "ERR:USE_START_ENDPOINT");
}

TEST_CASE("Core: start_job requires INIT", "[core]")
{
    core::Core c;

    {
        const auto r = c.start_job(1000);
        REQUIRE(r.ok == false);
        REQUIRE(r.http_status == 409);
        REQUIRE(r.command == "START");
        REQUIRE(r.message == "ERR:NOT_INIT");
        REQUIRE(r.state == core::State::NOT_INIT);
    }

    (void)c.handle_sync_command("INIT");

    {
        const auto r = c.start_job(1000);
        REQUIRE(r.ok == true);
        REQUIRE(r.http_status == 202);
        REQUIRE(r.command == "START");
        REQUIRE(r.message == "ACCEPTED");
        REQUIRE(r.state == core::State::RUNNING);
        REQUIRE(c.state() == core::State::RUNNING);
    }
}

TEST_CASE("Core: stop transitions to STOPPED", "[core]")
{
    core::Core c;
    (void)c.handle_sync_command("INIT");

    const auto r = c.stop();
    REQUIRE(r.ok == true);
    REQUIRE(r.http_status == 200);
    REQUIRE(r.command == "STOP");
    REQUIRE(r.message == "OK");
    REQUIRE(r.state == core::State::STOPPED);
    REQUIRE(c.state() == core::State::STOPPED);
}
