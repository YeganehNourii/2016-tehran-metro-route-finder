#pragma once
#include <string>

// A small hand-made network for exact tests. Coordinates are on the equator,
// where 0.01° of longitude is about 1112 m.
//
//   Line A:  P ── Q ── R             (short, straight)
//   Line B:  P ── S ── R ── T        (S is a long detour)
//   Line C:  X ── Y                  (not connected to anything)
inline std::string toy_network_json() {
    return R"({
      "stations": [
        {"id": "p", "name_en": "Park",     "name_fa": "پارک",   "lat": 0,    "lon": 0},
        {"id": "q", "name_en": "Queen",    "name_fa": "ملکه",   "lat": 0,    "lon": 0.01},
        {"id": "r", "name_en": "River",    "name_fa": "رود",    "lat": 0,    "lon": 0.02},
        {"id": "s", "name_en": "Summit",   "name_fa": "قله",    "lat": 0.03, "lon": 0.01},
        {"id": "t", "name_en": "Terminal", "name_fa": "پایانه", "lat": 0,    "lon": 0.03},
        {"id": "x", "name_en": "Xeno",     "lat": 1, "lon": 1},
        {"id": "y", "name_en": "Yard",     "lat": 1, "lon": 1.01}
      ],
      "lines": [
        {"id": "A", "name": "Line A", "color": "#ff0000", "stations": ["p", "q", "r"]},
        {"id": "B", "name": "Line B", "color": "#0000ff", "stations": ["p", "s", "r", "t"]},
        {"id": "C", "name": "Line C", "color": "#00ff00", "stations": ["x", "y"]}
      ]
    })";
}
