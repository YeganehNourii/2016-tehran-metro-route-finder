#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

#include "helpers.hpp"
#include "metro/network.hpp"

using metro::Network;
using metro::RouteOptions;

namespace {

metro::NetworkOptions exact() {
    metro::NetworkOptions o;
    o.speed_kmh = 36;
    o.dwell_seconds = 0;
    return o;
}

RouteOptions penalty(int seconds) {
    RouteOptions r;
    r.transfer_seconds = seconds;
    return r;
}

std::size_t id(const Network& n, const char* s) { return *n.station_by_id(s); }

}  // namespace

TEST_CASE("route along a single line", "[dijkstra]") {
    Network net = Network::from_json(toy_network_json(), exact());
    auto r = net.route(id(net, "p"), id(net, "r"));
    REQUIRE(r.found);
    REQUIRE(r.legs.size() == 1);
    CHECK(net.lines()[r.legs[0].line].id == "A");
    CHECK(r.legs[0].stations == std::vector<std::size_t>{0, 1, 2});
    CHECK(r.total_seconds == 2 * 111);
    CHECK(r.transfers() == 0);
    CHECK(r.transfer_seconds == 0);
}

TEST_CASE("transfer penalty decides between changing line and staying", "[dijkstra]") {
    Network net = Network::from_json(toy_network_json(), exact());
    std::size_t p = id(net, "p"), t = id(net, "t");
    int straight = net.ride_seconds(0, 1) + net.ride_seconds(1, 2);           // P→Q→R on A
    int detour = net.ride_seconds(0, 3) + net.ride_seconds(3, 2);             // P→S→R on B
    int last = net.ride_seconds(2, 4);                                        // R→T on B
    REQUIRE(detour - straight > 300);
    REQUIRE(detour - straight < 600);

    SECTION("cheap transfer: ride A, change at River, ride B") {
        auto r = net.route(p, t, penalty(300));
        REQUIRE(r.found);
        REQUIRE(r.legs.size() == 2);
        CHECK(net.lines()[r.legs[0].line].id == "A");
        CHECK(net.lines()[r.legs[1].line].id == "B");
        CHECK(r.legs[1].stations.front() == id(net, "r"));
        CHECK(r.total_seconds == straight + 300 + last);
        CHECK(r.transfer_seconds == 300);
    }
    SECTION("expensive transfer: stay on B the whole way") {
        auto r = net.route(p, t, penalty(600));
        REQUIRE(r.legs.size() == 1);
        CHECK(net.lines()[r.legs[0].line].id == "B");
        CHECK(r.total_seconds == detour + last);
    }
    SECTION("zero penalty never produces empty legs") {
        auto r = net.route(p, t, penalty(0));
        for (const auto& leg : r.legs)
            CHECK(leg.stations.size() >= 2);
    }
}

TEST_CASE("same station and unreachable station", "[dijkstra]") {
    Network net = Network::from_json(toy_network_json(), exact());
    auto same = net.route(id(net, "q"), id(net, "q"));
    CHECK(same.found);
    CHECK(same.legs.empty());
    CHECK(same.total_seconds == 0);

    auto none = net.route(id(net, "p"), id(net, "x"));
    CHECK_FALSE(none.found);
    CHECK_THROWS_AS(net.route(0, 999), std::out_of_range);
    CHECK_THROWS_AS(net.route(0, 1, penalty(-1)), std::invalid_argument);
}

TEST_CASE("routes are symmetric and legs add up", "[dijkstra]") {
    Network net = Network::from_json(toy_network_json(), exact());
    for (std::size_t a = 0; a < 5; ++a)
        for (std::size_t b = 0; b < 5; ++b) {
            auto ab = net.route(a, b), ba = net.route(b, a);
            CHECK(ab.total_seconds == ba.total_seconds);
            int sum = ab.transfer_seconds;
            for (const auto& leg : ab.legs)
                sum += leg.seconds;
            CHECK(sum == ab.total_seconds);
            if (!ab.legs.empty()) {
                CHECK(ab.legs.front().stations.front() == a);
                CHECK(ab.legs.back().stations.back() == b);
            }
        }
}

// Bellman-Ford is slow but obviously correct: use it to check Dijkstra.
static long long bellman_ford(const Network& net, std::size_t from, std::size_t to, int transfer) {
    const long long inf = std::numeric_limits<long long>::max() / 4;
    std::vector<long long> d(net.nodes().size(), inf);
    for (std::size_t u : net.nodes_of(from))
        d[u] = 0;
    for (std::size_t round = 0; round < net.nodes().size(); ++round) {
        bool changed = false;
        for (std::size_t u = 0; u < d.size(); ++u)
            for (const auto& e : net.edges()[u]) {
                long long w = e.kind == metro::Edge::Kind::Transfer ? transfer : e.seconds;
                if (d[u] + w < d[e.to]) {
                    d[e.to] = d[u] + w;
                    changed = true;
                }
            }
        if (!changed)
            break;
    }
    long long best = inf;
    for (std::size_t u : net.nodes_of(to))
        best = std::min(best, d[u]);
    return best;
}

TEST_CASE("Dijkstra matches Bellman-Ford on the real Tehran network", "[dijkstra][tehran]") {
    Network net = Network::load_file(METRO_DATA_FILE);
    std::mt19937 rng(2016);
    std::uniform_int_distribution<std::size_t> pick(0, net.stations().size() - 1);
    for (int transfer : {0, 180, 300, 900}) {
        for (int i = 0; i < 15; ++i) {
            std::size_t a = pick(rng), b = pick(rng);
            auto r = net.route(a, b, penalty(transfer));
            REQUIRE(r.found);
            CHECK(r.total_seconds == bellman_ford(net, a, b, transfer));
        }
    }
}
