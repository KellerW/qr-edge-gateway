#include <catch2/catch_test_macros.hpp>

#include "app/JobStore.hpp"

TEST_CASE("JobStore: create/exists/get default PENDING", "[jobstore]")
{
    app::JobStore store;

    const std::string id = "abc123";
    REQUIRE(store.exists(id) == false);

    store.create(id);
    REQUIRE(store.exists(id) == true);

    const auto r = store.get(id);
    REQUIRE(r.status == app::JobStatus::PENDING);
    REQUIRE(r.message == "PENDING");
    REQUIRE(r.qr.has_value() == false);
}

TEST_CASE("JobStore: set_done updates status/message/qr", "[jobstore]")
{
    app::JobStore store;
    const std::string id = "job1";
    store.create(id);

    store.set_done(id, "QR:999");
    const auto r = store.get(id);

    REQUIRE(r.status == app::JobStatus::DONE);
    REQUIRE(r.message == "OK");
    REQUIRE(r.qr.has_value() == true);
    REQUIRE(*r.qr == "QR:999");
}

TEST_CASE("JobStore: set_timeout updates status/message and clears qr", "[jobstore]")
{
    app::JobStore store;
    const std::string id = "job2";
    store.create(id);
    store.set_done(id, "QR:999");

    store.set_timeout(id);
    const auto r = store.get(id);

    REQUIRE(r.status == app::JobStatus::TIMEOUT);
    REQUIRE(r.message == "TIMEOUT");
    REQUIRE(r.qr.has_value() == false);
}
