// network.hpp - Station, Line and Network: the metro as a weighted graph.
#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace metro {

struct Station {
    std::string id;       // stable slug, e.g. "tajrish"
    std::string name_en;  // "Tajrish"
    std::string name_fa;  // "تجریش"
    double lat = 0.0;
    double lon = 0.0;
    std::vector<std::size_t> lines;  // indices into Network::lines()
};

struct Line {
    std::string id;     // "1", "1A" (branch), ...
    std::string name;   // "Line 1"
    std::string name_fa;
    std::string color;  // "#FF0F04"
    std::vector<std::size_t> stations;  // indices into Network::stations(), in order
};

// How travel times are estimated (OSM has no timetable).
struct NetworkOptions {
    double speed_kmh = 36.0;  // average speed between stations
    int dwell_seconds = 30;   // time stopped at each station
};

struct RouteOptions {
    int transfer_seconds = 300;  // penalty for changing line (walk + wait)
};

// One continuous ride on one line.
struct Leg {
    std::size_t line = 0;
    std::vector<std::size_t> stations;  // from first to last, inclusive
    int seconds = 0;
};

struct Route {
    bool found = false;
    int total_seconds = 0;   // rides + transfer penalties
    int transfer_seconds = 0;
    std::vector<Leg> legs;   // legs.size() - 1 changes of line
    std::size_t transfers() const { return legs.empty() ? 0 : legs.size() - 1; }
};

struct StationMatch {
    std::size_t station = 0;
    double score = 0.0;  // 1.0 = exact
};

class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The routing graph. Nodes are (station, line) pairs so that changing line
// can carry its own cost; edges are rides (travel time) or transfers.
struct GraphNode {
    std::size_t station;
    std::size_t line;
};

struct Edge {
    enum class Kind { Ride, Transfer };
    std::size_t to;
    int seconds;  // ride time; transfers use RouteOptions::transfer_seconds
    Kind kind;
};

class Network {
public:
    // Parses the JSON produced by data/fetch_osm.py. Throws DataError.
    static Network from_json(const std::string& text, const NetworkOptions& options = {});
    static Network load_file(const std::string& path, const NetworkOptions& options = {});

    const std::vector<Station>& stations() const { return stations_; }
    const std::vector<Line>& lines() const { return lines_; }
    const std::vector<GraphNode>& nodes() const { return nodes_; }
    const std::vector<std::vector<Edge>>& edges() const { return adj_; }
    const std::vector<std::size_t>& nodes_of(std::size_t station) const { return station_nodes_.at(station); }

    std::optional<std::size_t> station_by_id(std::string_view id) const;
    int ride_seconds(std::size_t a, std::size_t b) const;

    // Fuzzy search over English names, Persian names and ids (best first).
    std::vector<StationMatch> search(std::string_view query, std::size_t limit = 5) const;
    // Best match if its score is at least min_score.
    std::optional<std::size_t> resolve(std::string_view query, double min_score = 0.5) const;

    // Dijkstra's shortest path (see router.cpp).
    Route route(std::size_t from, std::size_t to, const RouteOptions& options = {}) const;

private:
    Network() = default;
    void build_graph();

    NetworkOptions options_;
    std::vector<Station> stations_;
    std::vector<Line> lines_;
    std::vector<GraphNode> nodes_;
    std::vector<std::vector<Edge>> adj_;
    std::vector<std::vector<std::size_t>> station_nodes_;
};

// Distance in metres between two coordinates (haversine formula).
double distance_m(double lat1, double lon1, double lat2, double lon2);

}  // namespace metro
