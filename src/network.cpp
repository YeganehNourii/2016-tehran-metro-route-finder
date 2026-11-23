#include "metro/network.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace metro {

namespace {

const nlohmann::json& require(const nlohmann::json& obj, const char* key, const std::string& where) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null())
        throw DataError(where + ": missing \"" + key + "\"");
    return *it;
}

std::string require_string(const nlohmann::json& obj, const char* key, const std::string& where) {
    const auto& v = require(obj, key, where);
    if (!v.is_string() || v.get<std::string>().empty())
        throw DataError(where + ": \"" + key + "\" must be a non-empty string");
    return v.get<std::string>();
}

double require_coord(const nlohmann::json& obj, const char* key, const std::string& where, double limit) {
    const auto& v = require(obj, key, where);
    if (!v.is_number())
        throw DataError(where + ": \"" + key + "\" must be a number");
    double d = v.get<double>();
    if (!std::isfinite(d) || std::fabs(d) > limit)
        throw DataError(where + ": \"" + key + "\" is out of range");
    return d;
}

}  // namespace

double distance_m(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEarthRadius = 6371000.0;
    auto rad = [](double deg) { return deg * kPi / 180.0; };
    double dlat = rad(lat2 - lat1), dlon = rad(lon2 - lon1);
    double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * kEarthRadius * std::asin(std::sqrt(std::min(1.0, h)));
}

Network Network::from_json(const std::string& text, const NetworkOptions& options) {
    if (options.speed_kmh <= 0 || options.dwell_seconds < 0)
        throw DataError("invalid network options");

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        throw DataError(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object())
        throw DataError("top level must be an object");
    const auto& jstations = require(root, "stations", "network");
    const auto& jlines = require(root, "lines", "network");
    if (!jstations.is_array() || !jlines.is_array())
        throw DataError("\"stations\" and \"lines\" must be arrays");

    Network net;
    net.options_ = options;
    std::map<std::string, std::size_t> index;

    for (std::size_t i = 0; i < jstations.size(); ++i) {
        const auto& js = jstations[i];
        std::string where = "station #" + std::to_string(i);
        if (!js.is_object())
            throw DataError(where + ": must be an object");
        Station s;
        s.id = require_string(js, "id", where);
        where = "station \"" + s.id + "\"";
        s.name_en = require_string(js, "name_en", where);
        s.name_fa = js.value("name_fa", "");
        s.lat = require_coord(js, "lat", where, 90);
        s.lon = require_coord(js, "lon", where, 180);
        if (!index.emplace(s.id, net.stations_.size()).second)
            throw DataError("duplicate station id \"" + s.id + "\"");
        net.stations_.push_back(std::move(s));
    }

    std::set<std::string> line_ids;
    for (std::size_t i = 0; i < jlines.size(); ++i) {
        const auto& jl = jlines[i];
        std::string where = "line #" + std::to_string(i);
        if (!jl.is_object())
            throw DataError(where + ": must be an object");
        Line line;
        line.id = require_string(jl, "id", where);
        where = "line \"" + line.id + "\"";
        if (!line_ids.insert(line.id).second)
            throw DataError("duplicate line id \"" + line.id + "\"");
        line.name = jl.value("name", "Line " + line.id);
        line.name_fa = jl.value("name_fa", "");
        line.color = jl.value("color", "#888888");
        const auto& seq = require(jl, "stations", where);
        if (!seq.is_array() || seq.size() < 2)
            throw DataError(where + ": needs at least two stations");
        for (const auto& sid : seq) {
            if (!sid.is_string())
                throw DataError(where + ": station ids must be strings");
            auto it = index.find(sid.get<std::string>());
            if (it == index.end())
                throw DataError(where + ": unknown station \"" + sid.get<std::string>() + "\"");
            if (std::find(line.stations.begin(), line.stations.end(), it->second) != line.stations.end())
                throw DataError(where + ": station \"" + it->first + "\" appears twice");
            line.stations.push_back(it->second);
        }
        std::size_t li = net.lines_.size();
        for (std::size_t s : line.stations)
            net.stations_[s].lines.push_back(li);
        net.lines_.push_back(std::move(line));
    }

    net.build_graph();
    return net;
}

Network Network::load_file(const std::string& path, const NetworkOptions& options) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw DataError("cannot open " + path);
    std::ostringstream buf;
    buf << in.rdbuf();
    return from_json(buf.str(), options);
}

int Network::ride_seconds(std::size_t a, std::size_t b) const {
    const Station& x = stations_.at(a);
    const Station& y = stations_.at(b);
    double metres = distance_m(x.lat, x.lon, y.lat, y.lon);
    double seconds = metres / (options_.speed_kmh / 3.6) + options_.dwell_seconds;
    return static_cast<int>(std::lround(seconds));
}

void Network::build_graph() {
    // One node per (station, line) pair.
    station_nodes_.assign(stations_.size(), {});
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> node_of;
    for (std::size_t li = 0; li < lines_.size(); ++li) {
        for (std::size_t s : lines_[li].stations) {
            node_of[{s, li}] = nodes_.size();
            station_nodes_[s].push_back(nodes_.size());
            nodes_.push_back({s, li});
        }
    }
    adj_.assign(nodes_.size(), {});

    // Ride edges between neighbouring stations, both directions.
    for (std::size_t li = 0; li < lines_.size(); ++li) {
        const auto& seq = lines_[li].stations;
        for (std::size_t i = 0; i + 1 < seq.size(); ++i) {
            std::size_t u = node_of[{seq[i], li}], v = node_of[{seq[i + 1], li}];
            int t = ride_seconds(seq[i], seq[i + 1]);
            adj_[u].push_back({v, t, Edge::Kind::Ride});
            adj_[v].push_back({u, t, Edge::Kind::Ride});
        }
    }

    // Transfer edges between every pair of lines at the same station.
    for (const auto& group : station_nodes_) {
        for (std::size_t a : group)
            for (std::size_t b : group)
                if (a != b)
                    adj_[a].push_back({b, 0, Edge::Kind::Transfer});
    }
}

std::optional<std::size_t> Network::station_by_id(std::string_view id) const {
    for (std::size_t i = 0; i < stations_.size(); ++i)
        if (stations_[i].id == id)
            return i;
    return std::nullopt;
}

}  // namespace metro
