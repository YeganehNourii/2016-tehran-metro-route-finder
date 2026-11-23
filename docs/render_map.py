#!/usr/bin/env python3
"""Draws docs/map.svg for the README: every line in its OSM colour, plus one
route computed by the real C++ program (`metro route ... --json`).

    cmake --build build && python3 docs/render_map.py Tajrish Azadi
"""
import html
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
data = json.loads((ROOT / "data" / "tehran_metro.json").read_text(encoding="utf-8"))
src, dst = (sys.argv[1:3] if len(sys.argv) >= 3 else ("Tajrish", "Azadi"))
route = json.loads(subprocess.run([str(ROOT / "build" / "metro"), "route", src, dst, "--json"],
                                  capture_output=True, text=True, check=True).stdout)

st = {s["id"]: s for s in data["stations"]}
# View: central Tehran plus the whole route (outlying branches are clipped).
view = [(35.62, 51.24), (35.81, 51.55)] + [(s["lat"], s["lon"]) for leg in route["legs"] for s in leg["stations"]]
lats = [v[0] for v in view]
lons = [v[1] for v in view]
W, PAD, PANEL = 900, 30, 300
kx = math.cos(math.radians(35.7))  # keep the map's proportions right
span_x, span_y = (max(lons) - min(lons)) * kx, max(lats) - min(lats)
scale = (W - PANEL - 2 * PAD) / span_x
H = int(span_y * scale + 2 * PAD)


def xy(s):
    return (PAD + (s["lon"] - min(lons)) * kx * scale, PAD + (max(lats) - s["lat"]) * scale)


out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
       'font-family="system-ui,-apple-system,Segoe UI,sans-serif">',
       f'<rect width="{W}" height="{H}" fill="#f4f1ea"/>',
       f'<clipPath id="map"><rect width="{W - PANEL}" height="{H}"/></clipPath><g clip-path="url(#map)">']
for line in data["lines"]:
    pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in (xy(st[i]) for i in line["stations"]))
    out.append(f'<polyline points="{pts}" fill="none" stroke="{line["color"]}" stroke-width="4" '
               'stroke-linejoin="round" stroke-linecap="round" opacity="0.35"/>')
for leg in route["legs"]:
    pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in (xy(s) for s in leg["stations"]))
    out.append(f'<polyline points="{pts}" fill="none" stroke="#fff" stroke-width="11" stroke-linejoin="round" stroke-linecap="round"/>')
    out.append(f'<polyline points="{pts}" fill="none" stroke="{leg["color"]}" stroke-width="7" stroke-linejoin="round" stroke-linecap="round"/>')
for s in data["stations"]:
    x, y = xy(s)
    out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="2.6" fill="#fff" stroke="#333" stroke-width="1"/>')
changes = [leg["stations"][0] for leg in route["legs"][1:]]
for s in [route["from"], route["to"], *changes]:
    x, y = xy(s)
    fill = "#ffd60a" if s in changes else "#c8102e"
    out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="7" fill="{fill}" stroke="#1d232a" stroke-width="2.5"/>')
    out.append(f'<text x="{x + 11:.1f}" y="{y + 4:.1f}" font-size="13" font-weight="700" fill="#1d232a" '
               f'stroke="#f4f1ea" stroke-width="4" paint-order="stroke">{html.escape(s["name_en"])}</text>')

out.append("</g>")
# Side panel with the steps, like the web demo.
px = W - PANEL + 10
out.append(f'<rect x="{W - PANEL}" y="0" width="{PANEL}" height="{H}" fill="#ffffff"/>')
y = 40
out.append(f'<text x="{px}" y="{y}" font-size="18" font-weight="700" fill="#1d232a">About {route["total_minutes"]} min</text>')
y += 22
n = route["transfers"]
out.append(f'<text x="{px}" y="{y}" font-size="13" fill="#5f6b76">{n} change{"s" if n != 1 else ""} of line</text>')
y += 34
for i, leg in enumerate(route["legs"]):
    if i:
        out.append(f'<text x="{px + 18}" y="{y}" font-size="12" fill="#5f6b76">change at {html.escape(leg["stations"][0]["name_en"])}</text>')
        y += 26
    out.append(f'<rect x="{px}" y="{y - 14}" width="6" height="38" rx="3" fill="{leg["color"]}"/>')
    out.append(f'<text x="{px + 18}" y="{y}" font-size="14" font-weight="600" fill="#1d232a">{html.escape(leg["name"])}</text>')
    out.append(f'<text x="{px + 18}" y="{y + 18}" font-size="12" fill="#1d232a">'
               f'{html.escape(leg["stations"][0]["name_en"])} → {html.escape(leg["stations"][-1]["name_en"])}</text>')
    out.append(f'<text x="{px + 18}" y="{y + 34}" font-size="11" fill="#5f6b76">{leg["stops"]} stops · {round(leg["seconds"] / 60)} min</text>')
    y += 62
out.append(f'<text x="{px}" y="{H - 16}" font-size="10" fill="#5f6b76">Map data © OpenStreetMap contributors (ODbL)</text>')
out.append("</svg>")
(ROOT / "docs" / "map.svg").write_text("\n".join(out) + "\n", encoding="utf-8")
print("wrote docs/map.svg")
