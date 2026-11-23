// Tests against the real Tehran Metro data in data/tehran_metro.json.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "metro/network.hpp"
#include "metro/route_json.hpp"

using metro::Network;
using Catch::Matchers::ContainsSubstring;

extern "C" {
int mt_init(const char* json_text);
const char* mt_route(const char* from, const char* to, int transfer_minutes);
const char* mt_search(const char* query, int limit);
}

namespace {
const Network& tehran() {
    static const Network net = Network::load_file(METRO_DATA_FILE);
    return net;
}
const std::string& name(std::size_t i) { return tehran().stations()[i].name_en; }
}  // namespace

TEST_CASE("data file looks like the Tehran Metro", "[tehran]") {
    const auto& net = tehran();
    CHECK(net.stations().size() > 100);
    CHECK(net.lines().size() >= 6);
    for (const auto& s : net.stations()) {
        CHECK(s.lat > 35.3);
        CHECK(s.lat < 36.0);
        CHECK(s.lon > 50.8);  // Parand (airport branch) is at 50.98
        CHECK(s.lon < 51.8);
        CHECK_FALSE(s.name_fa.empty());
    }
}

TEST_CASE("every station can reach every other station", "[tehran]") {
    const auto& net = tehran();
    for (std::size_t b = 1; b < net.stations().size(); ++b)
        CHECK(net.route(0, b).found);
}

TEST_CASE("fuzzy names find the right Tehran stations", "[tehran]") {
    const auto& net = tehran();
    CHECK(name(*net.resolve("Tajrish")) == "Tajrish");
    CHECK(name(*net.resolve("Imam Khomeini")) == "Emam Khomeini");
    CHECK(name(*net.resolve("imam khomeini airport")) == "Imam Khomeini Airport");
    CHECK(name(*net.resolve("azadi")) == "Meydan-e Azadi");
    CHECK(name(*net.resolve("تجریش")) == "Tajrish");
    CHECK(name(*net.resolve("teatr shahr")) == "Teatr-e Shahr");
}

TEST_CASE("Tajrish to Imam Khomeini is a direct ride on Line 1", "[tehran]") {
    const auto& net = tehran();
    auto r = net.route(*net.resolve("Tajrish"), *net.resolve("Imam Khomeini"));
    REQUIRE(r.found);
    REQUIRE(r.legs.size() == 1);
    CHECK(net.lines()[r.legs[0].line].id == "1");
    CHECK(r.total_seconds > 20 * 60);
    CHECK(r.total_seconds < 45 * 60);
}

TEST_CASE("Tajrish to Azadi needs a change of line", "[tehran]") {
    const auto& net = tehran();
    auto r = net.route(*net.resolve("Tajrish"), *net.resolve("Meydan-e Azadi"));
    REQUIRE(r.found);
    CHECK(r.transfers() >= 1);
    CHECK(net.lines()[r.legs.back().line].id == "4");
    // A huge penalty never adds more changes than needed.
    metro::RouteOptions slow;
    slow.transfer_seconds = 3600;
    CHECK(net.route(*net.resolve("Tajrish"), *net.resolve("Meydan-e Azadi"), slow).transfers() <= r.transfers());
}

TEST_CASE("JSON output and the WebAssembly bridge", "[tehran][json]") {
    const auto& net = tehran();
    auto a = *net.resolve("Tajrish"), b = *net.resolve("Meydan-e Azadi");
    std::string j = metro::route_to_json(net, net.route(a, b), a, b);
    CHECK_THAT(j, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(j, ContainsSubstring("\"legs\""));
    CHECK_THAT(j, ContainsSubstring("Tajrish"));

    // The same functions the browser calls (web/bindings.cpp), compiled natively.
    std::string text;
    {
        std::FILE* f = std::fopen(METRO_DATA_FILE, "rb");
        REQUIRE(f != nullptr);
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            text.append(buf, n);
        std::fclose(f);
    }
    CHECK(mt_init("{bad json") == -1);
    CHECK(mt_init(text.c_str()) == static_cast<int>(net.stations().size()));
    CHECK_THAT(std::string(mt_route("tajrish", "Imam Khomeini", 5)), ContainsSubstring("\"transfers\":0"));
    CHECK_THAT(std::string(mt_route("tajrish", "zzzzqqq", 5)), ContainsSubstring("unknown station"));
    CHECK_THAT(std::string(mt_route("tajrish", "azadi", 500)), ContainsSubstring("0-120"));
    CHECK_THAT(std::string(mt_search("sadeghi", 3)), ContainsSubstring("Sadeghiyeh"));
}
