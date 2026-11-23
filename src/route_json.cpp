#include "metro/route_json.hpp"

#include <nlohmann/json.hpp>

namespace metro {

namespace {

nlohmann::json station_json(const Station& s) {
    return {{"id", s.id}, {"name_en", s.name_en}, {"name_fa", s.name_fa}, {"lat", s.lat}, {"lon", s.lon}};
}

}  // namespace

std::string route_to_json(const Network& net, const Route& route, std::size_t from, std::size_t to) {
    nlohmann::json j;
    j["ok"] = route.found;
    j["from"] = station_json(net.stations().at(from));
    j["to"] = station_json(net.stations().at(to));
    if (!route.found) {
        j["error"] = "no route between these stations";
        return j.dump();
    }
    j["total_seconds"] = route.total_seconds;
    j["total_minutes"] = (route.total_seconds + 30) / 60;
    j["transfer_seconds"] = route.transfer_seconds;
    j["transfers"] = route.transfers();
    j["legs"] = nlohmann::json::array();
    for (const Leg& leg : route.legs) {
        const Line& line = net.lines().at(leg.line);
        nlohmann::json jl = {{"line", line.id}, {"name", line.name}, {"name_fa", line.name_fa},
                             {"color", line.color}, {"seconds", leg.seconds},
                             {"stops", leg.stations.size() - 1}};
        jl["stations"] = nlohmann::json::array();
        for (std::size_t s : leg.stations)
            jl["stations"].push_back(station_json(net.stations()[s]));
        j["legs"].push_back(std::move(jl));
    }
    return j.dump();
}

std::string matches_to_json(const Network& net, const std::vector<StationMatch>& matches) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& m : matches) {
        auto s = station_json(net.stations().at(m.station));
        s["score"] = m.score;
        j.push_back(std::move(s));
    }
    return j.dump();
}

std::string error_json(const std::string& message) {
    return nlohmann::json{{"ok", false}, {"error", message}}.dump();
}

}  // namespace metro
