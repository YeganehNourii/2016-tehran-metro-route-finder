// bindings.cpp - C functions exported to JavaScript (Emscripten build).
//
// The page fetches data/tehran_metro.json, passes the text to mt_init and
// then calls mt_route / mt_search. Results are JSON strings owned by this
// file and valid until the next call (JavaScript copies them right away).
#include <memory>
#include <optional>
#include <string>

#include "metro/network.hpp"
#include "metro/route_json.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT extern "C"
#endif

namespace {
std::unique_ptr<metro::Network> g_net;
std::string g_result;

const char* keep(std::string s) {
    g_result = std::move(s);
    return g_result.c_str();
}
}  // namespace

// Returns the number of stations, or -1 if the data is invalid.
EXPORT int mt_init(const char* json_text) {
    try {
        g_net = std::make_unique<metro::Network>(metro::Network::from_json(json_text));
        return static_cast<int>(g_net->stations().size());
    } catch (const std::exception& e) {
        g_net.reset();
        keep(e.what());
        return -1;
    }
}

EXPORT const char* mt_last_error() { return g_result.c_str(); }

// from/to are station ids or free-text names (fuzzy matched).
EXPORT const char* mt_route(const char* from, const char* to, int transfer_minutes) {
    if (!g_net)
        return keep(metro::error_json("network not loaded"));
    auto pick = [](const char* q) -> std::optional<std::size_t> {
        if (auto id = g_net->station_by_id(q))
            return id;
        return g_net->resolve(q);
    };
    auto a = pick(from), b = pick(to);
    if (!a || !b)
        return keep(metro::error_json(std::string("unknown station: ") + (!a ? from : to)));
    if (transfer_minutes < 0 || transfer_minutes > 120)
        return keep(metro::error_json("transfer penalty must be 0-120 minutes"));
    metro::RouteOptions opt;
    opt.transfer_seconds = transfer_minutes * 60;
    return keep(metro::route_to_json(*g_net, g_net->route(*a, *b, opt), *a, *b));
}

EXPORT const char* mt_search(const char* query, int limit) {
    if (!g_net)
        return keep("[]");
    return keep(metro::matches_to_json(*g_net, g_net->search(query, limit > 0 ? static_cast<std::size_t>(limit) : 5)));
}
