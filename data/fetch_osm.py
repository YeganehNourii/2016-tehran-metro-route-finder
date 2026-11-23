#!/usr/bin/env python3
"""Download Tehran Metro lines and stations from OpenStreetMap (Overpass API)
and write a clean JSON file: data/tehran_metro.json.

    python3 data/fetch_osm.py            # download + build the JSON
    python3 data/fetch_osm.py --list     # just list the route relations found
    python3 data/fetch_osm.py --raw raw.json   # build from a saved Overpass reply

Data © OpenStreetMap contributors, available under the Open Database License
(ODbL) 1.0: https://www.openstreetmap.org/copyright

How it works:
  * Each metro line in OSM is one or more "route" relations (usually one per
    direction). Their members with role stop/stop_entry_only/stop_exit_only
    (or station nodes) are the stops, in travel order.
  * For each line (ref = "1", "2", ...) we keep the direction with the most
    stops as the canonical order.
  * Platforms of the same station on different lines are separate OSM nodes,
    so stops are merged into one station when they share an English name and
    are within 600 m (that is what makes a transfer station).
Only the standard library is used.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ENDPOINTS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]
USER_AGENT = "tehran-metro-route-finder/1.0 (educational project; github.com/YeganehNourii)"
BBOX = (35.40, 51.00, 35.95, 51.75)  # south, west, north, east (Tehran + suburbs)

QUERY = f"""
[out:json][timeout:180];
relation["route"~"^(subway|light_rail)$"]({BBOX[0]},{BBOX[1]},{BBOX[2]},{BBOX[3]})->.routes;
.routes out body;
node(r.routes);
out body;
"""

STOP_ROLES = {"stop", "stop_entry_only", "stop_exit_only", "station", ""}
OUT = Path(__file__).resolve().parent / "tehran_metro.json"


def ssl_context() -> ssl.SSLContext:
    """Use certifi's CA bundle when installed (python.org builds on macOS
    ship without system certificates until "Install Certificates" is run)."""
    try:
        import certifi  # type: ignore

        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def fetch(query: str) -> dict:
    ctx = ssl_context()
    data = urllib.parse.urlencode({"data": query}).encode()
    delay = 5
    last_error: Exception | None = None
    for attempt in range(6):
        for url in ENDPOINTS:
            req = urllib.request.Request(url, data=data, headers={
                "User-Agent": USER_AGENT, "Accept": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=200, context=ctx) as resp:
                    return json.load(resp)
            except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
                last_error = exc
                print(f"  {url}: {exc}; trying next", file=sys.stderr)
                if "CERTIFICATE_VERIFY_FAILED" in str(exc):
                    raise SystemExit("SSL certificates missing: run 'pip install certifi' or, on macOS, "
                                     "'Install Certificates.command' from your Python folder.") from None
        print(f"  all servers busy, retrying in {delay}s", file=sys.stderr)
        time.sleep(delay)
        delay *= 2
    raise SystemExit(f"Overpass API unavailable: {last_error}")


def haversine_m(a: tuple[float, float], b: tuple[float, float]) -> float:
    lat1, lon1, lat2, lon2 = map(math.radians, (*a, *b))
    h = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2
    return 2 * 6_371_000 * math.asin(math.sqrt(h))


def clean_en(name: str) -> str:
    """'Tajrish Metro Station' -> 'Tajrish'."""
    name = re.sub(r"\s*\((?:metro|subway)[^)]*\)\s*", " ", name, flags=re.I)
    name = re.sub(r"\b(metro|subway|underground)?\s*station\b", "", name, flags=re.I)
    name = re.sub(r"^\s*(metro|subway)\s+", "", name, flags=re.I)
    return re.sub(r"\s+", " ", name).strip(" -–")


def clean_fa(name: str) -> str:
    name = re.sub(r"(ایستگاه|مترو|ي)", lambda m: "ی" if m.group(0) == "ي" else "", name)
    return re.sub(r"\s+", " ", name).strip(" -–")


def key_of(name_en: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name_en.lower())


def line_ref(tags: dict) -> str | None:
    ref = (tags.get("ref") or "").strip()
    if not ref:
        m = re.search(r"(?:line|خط)\s*([0-9]+[A-Za-z]?)", tags.get("name:en", "") + " " + tags.get("name", ""), re.I)
        ref = m.group(1) if m else ""
    return ref or None


NAMED_COLOURS = {"yellow": "#F2C500", "red": "#E0001B", "blue": "#0D5EAF", "green": "#2E9E44",
                 "orange": "#F08A00", "purple": "#6A00FF", "pink": "#FF5599", "brown": "#8B5A2B"}


def is_tehran(tags: dict) -> bool:
    net = (tags.get("network") or "") + " " + (tags.get("network:en") or "") + " " + (tags.get("operator") or "")
    return "تهران" in net or "tehran" in net.lower()


def stop_nodes(rel: dict, nodes: dict) -> list[dict]:
    stops: list[dict] = []
    for m in rel.get("members", []):
        if m["type"] != "node" or m.get("role", "") not in STOP_ROLES:
            continue
        node = nodes.get(m["ref"])
        if not node or "name" not in node.get("tags", {}):
            continue
        if m.get("role", "") == "" and node["tags"].get("railway") not in ("station", "stop", "halt"):
            continue
        if stops and stops[-1]["id"] == node["id"]:
            continue
        stops.append(node)
    return stops


def node_key(node: dict) -> str:
    t = node["tags"]
    return key_of(clean_en(t.get("name:en") or t.get("int_name") or t["name"]))


def build(raw: dict) -> dict:
    nodes = {e["id"]: e for e in raw["elements"] if e["type"] == "node"}
    relations = [e for e in raw["elements"] if e["type"] == "relation" and is_tehran(e["tags"])]

    # 1) per line ref: the longest relation is the main line; a relation that
    #    visits stations the accepted ones do not is a branch; anything else
    #    (the opposite direction, partial services) is skipped.
    by_ref: dict[str, list[tuple[dict, list[dict]]]] = {}
    for rel in relations:
        ref = line_ref(rel["tags"])
        stops = stop_nodes(rel, nodes)
        if ref and len(stops) >= 2:
            by_ref.setdefault(ref, []).append((rel, stops))
    best: dict[str, tuple[dict, list[dict]]] = {}
    for ref, cands in by_ref.items():
        cands.sort(key=lambda c: (-len(c[1]), c[0]["id"]))
        accepted: list[set[str]] = []
        for rel, stops in cands:
            keys = {node_key(n) for n in stops}
            if any(keys <= a for a in accepted):
                continue
            line_id = ref if not accepted else f"{ref}{'ABCDEFG'[len(accepted) - 1]}"
            accepted.append(keys)
            best[line_id] = (rel, stops)

    # 2) merge stops into stations (same English name, close together)
    stations: list[dict] = []
    by_key: dict[str, list[int]] = {}

    def station_for(node: dict) -> int:
        tags = node["tags"]
        en = clean_en(tags.get("name:en") or tags.get("int_name") or tags.get("name:en-US") or tags["name"])
        fa = clean_fa(tags.get("name:fa") or tags["name"])
        pos = (node["lat"], node["lon"])
        k = key_of(en)
        for idx in by_key.get(k, []):
            if haversine_m(pos, (stations[idx]["lat"], stations[idx]["lon"])) < 600:
                return idx
        stations.append({"name_en": en, "name_fa": fa, "lat": round(pos[0], 6), "lon": round(pos[1], 6)})
        by_key.setdefault(k, []).append(len(stations) - 1)
        return len(stations) - 1

    lines = []
    for ref in sorted(best, key=lambda r: (int(re.match(r"\d+", r).group()) if re.match(r"\d+", r) else 99, r)):
        rel, stops = best[ref]
        tags = rel["tags"]
        colour = tags.get("colour", "#888888")
        colour = NAMED_COLOURS.get(colour.lower(), colour)
        if re.fullmatch(r"#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?", colour) is None:
            colour = "#888888"
        base_ref = re.match(r"\d+", ref).group() if re.match(r"\d+", ref) else ref
        branch = ref != base_ref
        seq: list[int] = []
        for node in stops:
            idx = station_for(node)
            if idx not in seq:  # no loops / repeated stops
                seq.append(idx)
        lines.append({
            "id": ref,
            "name": f"Line {base_ref}" + (" (branch)" if branch else ""),
            "name_fa": f"خط {base_ref}" + (" (انشعاب)" if branch else ""),
            "color": colour.upper(),
            "osm_relation": rel["id"],
            "stations": seq,
        })

    # 3) give stations stable ids (slugs), drop stations no line uses
    used = sorted({i for line in lines for i in line["stations"]}, key=lambda i: stations[i]["name_en"])
    slug_of: dict[int, str] = {}
    taken: set[str] = set()
    for i in used:
        base = re.sub(r"[^a-z0-9]+", "-", stations[i]["name_en"].lower()).strip("-") or f"station-{i}"
        slug, n = base, 2
        while slug in taken:
            slug, n = f"{base}-{n}", n + 1
        taken.add(slug)
        slug_of[i] = slug
    for line in lines:
        line["stations"] = [slug_of[i] for i in line["stations"]]

    return {
        "source": "OpenStreetMap contributors, via Overpass API",
        "license": "ODbL 1.0 (https://www.openstreetmap.org/copyright)",
        "generated": raw.get("osm3s", {}).get("timestamp_osm_base", ""),
        "stations": [{"id": slug_of[i], **stations[i]} for i in used],
        "lines": lines,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true", help="only list route relations")
    ap.add_argument("--raw", help="build from a saved Overpass JSON file instead of downloading")
    ap.add_argument("--save-raw", help="also save the raw Overpass reply here")
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()

    if args.raw:
        raw = json.loads(Path(args.raw).read_text(encoding="utf-8"))
    else:
        print("Querying Overpass API…", file=sys.stderr)
        raw = fetch(QUERY)
        if args.save_raw:
            Path(args.save_raw).write_text(json.dumps(raw), encoding="utf-8")

    if args.list:
        for e in raw["elements"]:
            if e["type"] == "relation":
                t = e["tags"]
                n = sum(1 for m in e.get("members", []) if m["type"] == "node")
                print(f'{e["id"]:>10} {t.get("route"):<10} ref={t.get("ref")!s:<4} nodes={n:<3} '
                      f'{t.get("name:en") or t.get("name")} | {t.get("network")} | {t.get("colour")}')
        return

    result = build(raw)
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"Wrote {args.out}: {len(result['stations'])} stations, {len(result['lines'])} lines", file=sys.stderr)
    for line in result["lines"]:
        print(f"  Line {line['id']:<4} {len(line['stations']):>3} stations  {line['color']}", file=sys.stderr)


if __name__ == "__main__":
    main()
