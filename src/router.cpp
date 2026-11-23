// Dijkstra's shortest path on the (station, line) graph.
#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

#include "metro/network.hpp"

namespace metro {

Route Network::route(std::size_t from, std::size_t to, const RouteOptions& options) const {
    if (from >= stations_.size() || to >= stations_.size())
        throw std::out_of_range("station index out of range");
    if (options.transfer_seconds < 0)
        throw std::invalid_argument("transfer penalty must not be negative");

    Route result;
    if (from == to) {
        result.found = true;
        return result;
    }

    constexpr long long kInf = std::numeric_limits<long long>::max();
    const std::size_t n = nodes_.size();
    std::vector<long long> dist(n, kInf);
    std::vector<std::size_t> prev(n, n);  // n = "no predecessor"

    // Min-heap of (distance, node). std::priority_queue has no decrease-key,
    // so we push a new entry when a distance improves and skip stale
    // entries when they are popped ("lazy deletion"). O((V + E) log V).
    using Item = std::pair<long long, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;

    // Multi-source start: every line at the origin station costs 0.
    for (std::size_t u : station_nodes_[from]) {
        dist[u] = 0;
        heap.push({0, u});
    }

    std::size_t goal = n;
    while (!heap.empty()) {
        auto [d, u] = heap.top();
        heap.pop();
        if (d != dist[u])
            continue;  // stale entry
        if (nodes_[u].station == to) {
            goal = u;  // the first time we pop the destination, it is optimal
            break;
        }
        for (const Edge& e : adj_[u]) {
            long long w = e.kind == Edge::Kind::Transfer ? options.transfer_seconds : e.seconds;
            if (d + w < dist[e.to]) {
                dist[e.to] = d + w;
                prev[e.to] = u;
                heap.push({dist[e.to], e.to});
            }
        }
    }
    if (goal == n)
        return result;  // not reachable

    // Walk back from the goal, then turn the node path into legs.
    std::vector<std::size_t> path;
    for (std::size_t u = goal; u != n; u = prev[u])
        path.push_back(u);
    std::reverse(path.begin(), path.end());

    std::vector<Leg> legs;
    std::size_t leg_start = 0;
    for (std::size_t i = 1; i <= path.size(); ++i) {
        bool end_of_leg = i == path.size() || nodes_[path[i]].line != nodes_[path[i - 1]].line;
        if (!end_of_leg)
            continue;
        Leg leg;
        leg.line = nodes_[path[leg_start]].line;
        for (std::size_t k = leg_start; k < i; ++k)
            leg.stations.push_back(nodes_[path[k]].station);
        leg.seconds = static_cast<int>(dist[path[i - 1]] - dist[path[leg_start]]);
        if (leg.stations.size() >= 2)  // skip zero-length "legs" (only possible with a 0 penalty)
            legs.push_back(std::move(leg));
        leg_start = i;
    }

    result.found = true;
    result.total_seconds = static_cast<int>(dist[goal]);
    result.legs = std::move(legs);
    int rides = 0;
    for (const Leg& leg : result.legs)
        rides += leg.seconds;
    result.transfer_seconds = result.total_seconds - rides;
    return result;
}

}  // namespace metro
