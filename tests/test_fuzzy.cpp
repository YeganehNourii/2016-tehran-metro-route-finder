#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"
#include "metro/fuzzy.hpp"
#include "metro/network.hpp"

using metro::levenshtein;
using metro::normalize;
using metro::similarity;

TEST_CASE("normalize", "[fuzzy]") {
    CHECK(normalize("Meydan-e Azadi") == "meydaneazadi");
    CHECK(normalize("  Sa'di ") == "sadi");
    CHECK(normalize("TAJRISH") == "tajrish");
    CHECK(normalize("") == "");
    // Arabic yeh (ي) and Persian yeh (ی) are treated as the same letter.
    CHECK(normalize("تجريش") == normalize("تجریش"));
    // Spaces and the zero-width non-joiner are ignored in Persian too.
    CHECK(normalize("میدان آزادی") == normalize("میدان‌آزادی"));
}

TEST_CASE("levenshtein", "[fuzzy]") {
    CHECK(levenshtein("", "") == 0);
    CHECK(levenshtein("abc", "") == 3);
    CHECK(levenshtein("kitten", "sitting") == 3);
    CHECK(levenshtein("imam", "emam") == 1);
    CHECK(levenshtein("same", "same") == 0);
}

TEST_CASE("similarity prefers whole-name matches", "[fuzzy]") {
    CHECK(similarity("tajrish", "tajrish") == 1.0);
    CHECK(similarity("", "x") == 0.0);
    // Typo in the whole name beats the same text buried in a longer name.
    CHECK(similarity("imamkhomeini", "emamkhomeini") > similarity("imamkhomeini", "harammotahareimamkhomeini"));
    CHECK(similarity("azadi", "meydaneazadi") > 0.7);
    CHECK(similarity("xyz", "tajrish") < 0.5);
}

TEST_CASE("search and resolve on a network", "[fuzzy]") {
    auto net = metro::Network::from_json(toy_network_json());
    CHECK(net.resolve("river") == net.station_by_id("r"));
    CHECK(net.resolve("Rivr") == net.station_by_id("r"));
    CHECK(net.resolve("پارک") == net.station_by_id("p"));
    CHECK(net.resolve("term") == net.station_by_id("t"));
    CHECK_FALSE(net.resolve("zzzzzz").has_value());
    CHECK_FALSE(net.resolve("   ").has_value());
    auto top = net.search("a", 3);
    CHECK(top.size() == 3);
    CHECK(top[0].score >= top[1].score);
}
