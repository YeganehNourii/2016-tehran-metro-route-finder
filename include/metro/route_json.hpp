// route_json.hpp - JSON output used by the web demo and `metro --json`.
#pragma once

#include <string>
#include <vector>

#include "metro/network.hpp"

namespace metro {

std::string route_to_json(const Network& net, const Route& route, std::size_t from, std::size_t to);
std::string matches_to_json(const Network& net, const std::vector<StationMatch>& matches);
std::string error_json(const std::string& message);

}  // namespace metro
