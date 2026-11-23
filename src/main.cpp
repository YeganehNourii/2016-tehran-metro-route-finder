// metro - command-line route finder for the Tehran Metro.
//
//   metro route "Tajrish" "Imam Khomeini" [--transfer MINUTES] [--json]
//   metro stations [QUERY]
//   metro lines
//
// Data file: --data FILE, else $METRO_DATA, else the file found at build time.
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "metro/fuzzy.hpp"
#include "metro/network.hpp"
#include "metro/route_json.hpp"

#ifndef METRO_DATA_FILE
#define METRO_DATA_FILE "data/tehran_metro.json"
#endif

namespace {

void usage(std::ostream& out) {
    out << "Usage:\n"
           "  metro route FROM TO [--transfer MINUTES] [--json]   fastest route\n"
           "  metro stations [QUERY]                              list or search stations\n"
           "  metro lines                                         list lines\n"
           "Options: --data FILE (default: $METRO_DATA or " METRO_DATA_FILE ")\n"
           "Station names are matched loosely: \"imam khomeini\", \"azadi\", \"تجریش\" all work.\n";
}

std::string minutes(int seconds) {
    return std::to_string((seconds + 30) / 60) + " min";
}

std::string label(const metro::Station& s) {
    return s.name_fa.empty() ? s.name_en : s.name_en + " (" + s.name_fa + ")";
}

// Resolves a station name, printing what was matched when it is not exact.
bool resolve(const metro::Network& net, const std::string& query, std::size_t& out) {
    auto matches = net.search(query, 3);
    if (matches.empty() || matches[0].score < 0.5) {
        std::cerr << "error: no station matches \"" << query << "\"";
        if (!matches.empty())
            std::cerr << " (closest: " << net.stations()[matches[0].station].name_en << ")";
        std::cerr << "\n";
        return false;
    }
    out = matches[0].station;
    if (matches[0].score < 1.0)
        std::cerr << "\"" << query << "\" → " << net.stations()[out].name_en << "\n";
    return true;
}

int cmd_route(const metro::Network& net, const std::vector<std::string>& args) {
    std::vector<std::string> names;
    metro::RouteOptions options;
    bool json = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") {
            json = true;
        } else if (args[i] == "--transfer" && i + 1 < args.size()) {
            char* end = nullptr;
            long m = std::strtol(args[++i].c_str(), &end, 10);
            if (*end != '\0' || m < 0 || m > 120) {
                std::cerr << "error: --transfer takes minutes between 0 and 120\n";
                return 1;
            }
            options.transfer_seconds = static_cast<int>(m * 60);
        } else {
            names.push_back(args[i]);
        }
    }
    if (names.size() != 2) {
        std::cerr << "error: usage: metro route FROM TO\n";
        return 1;
    }
    std::size_t from = 0, to = 0;
    if (!resolve(net, names[0], from) || !resolve(net, names[1], to))
        return 2;

    metro::Route r = net.route(from, to, options);
    if (json) {
        std::cout << metro::route_to_json(net, r, from, to) << "\n";
        return r.found ? 0 : 3;
    }
    const auto& st = net.stations();
    std::cout << "From " << label(st[from]) << " to " << label(st[to]) << "\n\n";
    if (!r.found) {
        std::cout << "No route found.\n";
        return 3;
    }
    if (r.legs.empty()) {
        std::cout << "You are already there.\n";
        return 0;
    }
    for (std::size_t i = 0; i < r.legs.size(); ++i) {
        const auto& leg = r.legs[i];
        const auto& line = net.lines()[leg.line];
        if (i > 0)
            std::cout << "  ↳ change at " << st[leg.stations.front()].name_en << " (+"
                      << minutes(options.transfer_seconds) << ")\n";
        std::size_t stops = leg.stations.size() - 1;
        std::cout << "  " << std::left << std::setw(16) << line.name << st[leg.stations.front()].name_en << " → "
                  << st[leg.stations.back()].name_en << "   " << stops << (stops == 1 ? " stop, " : " stops, ")
                  << minutes(leg.seconds) << "\n";
        for (std::size_t k = 1; k + 1 < leg.stations.size(); ++k)
            std::cout << "  " << std::setw(16) << "" << "· " << st[leg.stations[k]].name_en << "\n";
    }
    std::cout << "\nTotal: about " << minutes(r.total_seconds) << ", " << r.transfers()
              << (r.transfers() == 1 ? " change" : " changes") << " of line\n";
    return 0;
}

int cmd_stations(const metro::Network& net, const std::vector<std::string>& args) {
    const auto& st = net.stations();
    auto print = [&](std::size_t i) {
        std::string lines;
        for (std::size_t l : st[i].lines)
            lines += (lines.empty() ? "" : ",") + net.lines()[l].id;
        std::cout << std::left << std::setw(36) << st[i].name_en << std::setw(10) << lines << st[i].name_fa << "\n";
    };
    if (args.empty()) {
        for (std::size_t i = 0; i < st.size(); ++i)
            print(i);
        return 0;
    }
    std::string q;
    for (const auto& a : args)
        q += (q.empty() ? "" : " ") + a;
    for (const auto& m : net.search(q, 8))
        if (m.score >= 0.5)
            print(m.station);
    return 0;
}

int cmd_lines(const metro::Network& net) {
    for (const auto& line : net.lines()) {
        const auto& st = net.stations();
        std::cout << std::left << std::setw(20) << line.name << std::setw(4) << line.stations.size() << "stations  "
                  << st[line.stations.front()].name_en << " ↔ " << st[line.stations.back()].name_en << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string data = std::getenv("METRO_DATA") ? std::getenv("METRO_DATA") : METRO_DATA_FILE;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--data") {
            data = args[i + 1];
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
            break;
        }
    }
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        usage(args.empty() ? std::cerr : std::cout);
        return args.empty() ? 1 : 0;
    }
    try {
        metro::Network net = metro::Network::load_file(data);
        std::string cmd = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (cmd == "route")
            return cmd_route(net, rest);
        if (cmd == "stations")
            return cmd_stations(net, rest);
        if (cmd == "lines")
            return cmd_lines(net);
        std::cerr << "error: unknown command '" << cmd << "'\n";
        usage(std::cerr);
        return 1;
    } catch (const metro::DataError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 4;
    }
}
