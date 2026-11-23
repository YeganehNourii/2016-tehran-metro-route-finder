#include "metro/fuzzy.hpp"

#include <algorithm>
#include <vector>

#include "metro/network.hpp"

namespace metro {

std::string normalize(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z')
                out.push_back(static_cast<char>(c - 'A' + 'a'));
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                out.push_back(static_cast<char>(c));
            continue;  // drop spaces and ASCII punctuation
        }
        if (i + 1 < text.size()) {
            unsigned char d = static_cast<unsigned char>(text[i + 1]);
            if (c == 0xD9 && d == 0x8A) { out += "\xDB\x8C"; ++i; continue; }  // Arabic yeh -> Persian yeh
            if (c == 0xD9 && d == 0x83) { out += "\xDA\xA9"; ++i; continue; }  // Arabic kaf -> Persian kaf
            if (c == 0xE2 && d == 0x80 && i + 2 < text.size()) { i += 2; continue; }  // ZWNJ, dashes, quotes
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::size_t levenshtein(std::string_view a, std::string_view b) {
    // Two-row dynamic programming: O(|a|·|b|) time, O(|b|) memory.
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

double similarity(std::string_view q, std::string_view c) {
    if (q.empty() || c.empty())
        return 0.0;
    if (q == c)
        return 1.0;
    double edit = 1.0 - static_cast<double>(levenshtein(q, c)) / static_cast<double>(std::max(q.size(), c.size()));
    double sub = 0.0;
    std::size_t pos = c.find(q);
    if (pos != std::string_view::npos) {
        // "azadi" inside "meydaneazadi": good, but an exact-ish whole name wins.
        sub = 0.7 + 0.25 * static_cast<double>(q.size()) / static_cast<double>(c.size());
        if (pos == 0)
            sub += 0.04;
    }
    return std::max(edit, sub);
}

std::vector<StationMatch> Network::search(std::string_view query, std::size_t limit) const {
    std::string q = normalize(query);
    std::vector<StationMatch> all;
    if (q.empty())
        return all;
    for (std::size_t i = 0; i < stations_.size(); ++i) {
        const Station& s = stations_[i];
        double score = std::max({similarity(q, normalize(s.name_en)), similarity(q, normalize(s.name_fa)),
                                 similarity(q, normalize(s.id))});
        all.push_back({i, score});
    }
    std::stable_sort(all.begin(), all.end(), [this](const StationMatch& a, const StationMatch& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return stations_[a.station].name_en < stations_[b.station].name_en;
    });
    if (all.size() > limit)
        all.resize(limit);
    return all;
}

std::optional<std::size_t> Network::resolve(std::string_view query, double min_score) const {
    auto best = search(query, 1);
    if (best.empty() || best[0].score < min_score)
        return std::nullopt;
    return best[0].station;
}

}  // namespace metro
