#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "helpers.hpp"
#include "metro/network.hpp"

using metro::DataError;
using metro::Network;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("loads stations and lines from JSON", "[network]") {
    Network net = Network::from_json(toy_network_json());
    REQUIRE(net.stations().size() == 7);
    REQUIRE(net.lines().size() == 3);
    CHECK(net.stations()[0].name_fa == "پارک");
    CHECK(net.lines()[1].color == "#0000ff");
    CHECK(net.station_by_id("r") == 2u);
    CHECK_FALSE(net.station_by_id("nope").has_value());
    // R is on lines A and B: a transfer station.
    CHECK(net.stations()[2].lines.size() == 2);
}

TEST_CASE("graph has one node per (station, line) and the right edges", "[network]") {
    Network net = Network::from_json(toy_network_json());
    // A: 3 nodes, B: 4 nodes, C: 2 nodes
    REQUIRE(net.nodes().size() == 9);
    std::size_t rides = 0, transfers = 0;
    for (const auto& list : net.edges())
        for (const auto& e : list)
            (e.kind == metro::Edge::Kind::Ride ? rides : transfers)++;
    CHECK(rides == 2 * (2 + 3 + 1));  // both directions
    CHECK(transfers == 2 * 2);        // P and R, each A<->B
    CHECK(net.nodes_of(0).size() == 2);
}

TEST_CASE("ride time comes from distance, speed and dwell", "[network]") {
    metro::NetworkOptions opt;
    opt.speed_kmh = 36;  // 10 m/s
    opt.dwell_seconds = 0;
    Network net = Network::from_json(toy_network_json(), opt);
    // 0.01° of longitude at the equator ≈ 1111.95 m
    CHECK(net.ride_seconds(0, 1) == 111);
    CHECK(metro::distance_m(0, 0, 0, 0.01) == Catch::Approx(1111.95).margin(0.1));
    CHECK(metro::distance_m(35.7, 51.4, 35.7, 51.4) == 0.0);
}

TEST_CASE("invalid data is rejected with a clear message", "[network]") {
    auto bad = [](const std::string& json) { return [json] { (void)Network::from_json(json); }; };
    CHECK_THROWS_WITH(bad("{oops")(), ContainsSubstring("invalid JSON"));
    CHECK_THROWS_WITH(bad("[]")(), ContainsSubstring("object"));
    CHECK_THROWS_WITH(bad(R"({"stations": []})")(), ContainsSubstring("lines"));
    CHECK_THROWS_WITH(
        bad(R"({"stations": [{"id":"a","name_en":"A","lat":0,"lon":0},{"id":"a","name_en":"B","lat":0,"lon":0}], "lines": []})")(),
        ContainsSubstring("duplicate station"));
    CHECK_THROWS_WITH(
        bad(R"({"stations": [{"id":"a","name_en":"A","lat":0,"lon":0}], "lines": [{"id":"1","stations":["a","zz"]}]})")(),
        ContainsSubstring("unknown station"));
    CHECK_THROWS_WITH(
        bad(R"({"stations": [{"id":"a","name_en":"A","lat":0,"lon":0}], "lines": [{"id":"1","stations":["a"]}]})")(),
        ContainsSubstring("at least two"));
    CHECK_THROWS_WITH(bad(R"({"stations": [{"id":"a","name_en":"A","lat":95,"lon":0}], "lines": []})")(),
                      ContainsSubstring("out of range"));
    CHECK_THROWS_WITH(bad(R"({"stations": [{"id":"a","lat":0,"lon":0}], "lines": []})")(),
                      ContainsSubstring("name_en"));
    CHECK_THROWS_AS(Network::load_file("/no/such/file.json"), DataError);
}
