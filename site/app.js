// Tehran Metro demo: Leaflet map + the C++ route engine (WebAssembly).
(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  let engine = null;   // { route(from, to, minutes), search(q, n) }
  let data = null;     // tehran_metro.json
  let byId = {};       // station id -> station
  let networkLayer, routeLayer, markers = {};
  let picks = { from: null, to: null };

  const map = L.map("map", { zoomControl: true }).setView([35.70, 51.39], 11);
  L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 18,
    attribution: '© <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  }).addTo(map);

  function status(text, isError) {
    $("status").textContent = text;
    $("status").classList.toggle("err", !!isError);
  }

  function label(s) { return `${s.name_en} — ${s.name_fa}`; }

  // ---- startup ------------------------------------------------------------

  async function boot() {
    if (typeof createMetroModule !== "function") {
      status("metro.js is missing: build it with Emscripten (see README).", true);
      return;
    }
    try {
      const [Module, text] = await Promise.all([
        createMetroModule(),
        fetch("data/tehran_metro.json").then((r) => r.text()),
      ]);
      const init = Module.cwrap("mt_init", "number", ["string"]);
      const n = init(text);
      if (n < 0) throw new Error(Module.cwrap("mt_last_error", "string", [])());
      engine = {
        route: Module.cwrap("mt_route", "string", ["string", "string", "number"]),
        search: Module.cwrap("mt_search", "string", ["string", "number"]),
      };
      data = JSON.parse(text);
      data.stations.forEach((s) => (byId[s.id] = s));
      drawNetwork();
      $("station-list").replaceChildren(...data.stations.map((s) => {
        const o = document.createElement("option");
        o.value = label(s);
        return o;
      }));
      document.querySelectorAll("input, button").forEach((x) => (x.disabled = false));
      status(`${n} stations, ${data.lines.length} lines loaded. Click a station or type a name.`);
      readHash();
    } catch (err) {
      status("Could not start: " + err.message, true);
    }
  }

  function drawNetwork() {
    networkLayer = L.layerGroup().addTo(map);
    const transfers = new Set();
    const seen = {};
    for (const line of data.lines) {
      L.polyline(line.stations.map((id) => [byId[id].lat, byId[id].lon]),
        { color: line.color, weight: 5, opacity: 0.85 }).bindTooltip(line.name, { sticky: true }).addTo(networkLayer);
      line.stations.forEach((id) => { if (seen[id]) transfers.add(id); seen[id] = true; });
    }
    for (const s of data.stations) {
      const big = transfers.has(s.id);
      markers[s.id] = L.circleMarker([s.lat, s.lon], {
        radius: big ? 6 : 4, baseRadius: big ? 6 : 4, color: "#1d232a", weight: big ? 2 : 1.5, fillColor: "#fff", fillOpacity: 1,
      }).bindTooltip(`${s.name_en}<br><span class="fa">${s.name_fa}</span>`, { direction: "top" })
        .on("click", () => pick(s.id))
        .addTo(networkLayer);
    }
    map.fitBounds(L.latLngBounds(data.stations.map((s) => [s.lat, s.lon])), { padding: [20, 20], animate: false });
  }

  // ---- picking stations ---------------------------------------------------

  // Turns typed text (or "Name — نام" from the list) into a station id via the C++ fuzzy search.
  function resolve(text) {
    text = text.split(" — ")[0].trim();
    if (!text) return null;
    const best = JSON.parse(engine.search(text, 1))[0];
    return best && best.score >= 0.5 ? best.id : null;
  }

  function pick(id) {
    if (!picks.from || (picks.from && picks.to)) {
      picks = { from: id, to: null };
      $("from").value = label(byId[id]);
      $("to").value = "";
      clearRoute();
      highlight();
      status(`From ${byId[id].name_en}. Now pick the destination.`);
    } else {
      picks.to = id;
      $("to").value = label(byId[id]);
      findRoute();
    }
  }

  function highlight() {
    for (const [id, m] of Object.entries(markers)) {
      const on = id === picks.from || id === picks.to;
      m.setStyle({ fillColor: on ? "#c8102e" : "#fff", radius: on ? 8 : m.options.baseRadius });
    }
  }

  // ---- routing ------------------------------------------------------------

  function clearRoute() {
    if (routeLayer) map.removeLayer(routeLayer);
    routeLayer = null;
    networkLayer.eachLayer((l) => l instanceof L.Polyline && !(l instanceof L.CircleMarker) && l.setStyle({ opacity: 0.85 }));
    $("result").replaceChildren();
  }

  function towards(lineId, fromId, toId) {
    const seq = data.lines.find((l) => l.id === lineId).stations;
    const end = seq.indexOf(toId) > seq.indexOf(fromId) ? seq[seq.length - 1] : seq[0];
    return byId[end].name_en;
  }

  function findRoute() {
    const fromText = $("from").value, toText = $("to").value;
    picks.from = picks.from && $("from").value === label(byId[picks.from]) ? picks.from : resolve(fromText);
    picks.to = picks.to && $("to").value === label(byId[picks.to]) ? picks.to : resolve(toText);
    if (!picks.from) return status(`No station matches "${fromText}".`, true);
    if (!picks.to) return status(`No station matches "${toText}".`, true);
    $("from").value = label(byId[picks.from]);
    $("to").value = label(byId[picks.to]);

    const minutes = Number($("penalty").value);
    const r = JSON.parse(engine.route(picks.from, picks.to, minutes));
    clearRoute();
    highlight();
    location.hash = `from=${picks.from}&to=${picks.to}&change=${minutes}`;
    if (!r.ok) return status(r.error, true);
    status("");
    render(r, minutes);
  }

  function render(r, minutes) {
    const out = $("result");
    if (r.legs.length === 0) {
      out.append(Object.assign(document.createElement("p"), { className: "summary", textContent: "You are already there." }));
      return;
    }
    networkLayer.eachLayer((l) => l instanceof L.Polyline && !(l instanceof L.CircleMarker) && l.setStyle({ opacity: 0.25 }));
    routeLayer = L.layerGroup().addTo(map);
    const pts = [];
    for (const leg of r.legs) {
      const ll = leg.stations.map((s) => [s.lat, s.lon]);
      pts.push(...ll);
      L.polyline(ll, { color: "#fff", weight: 11, opacity: 1 }).addTo(routeLayer);
      L.polyline(ll, { color: leg.color, weight: 7, opacity: 1 }).addTo(routeLayer);
    }
    r.legs.slice(1).forEach((leg) => {
      const s = leg.stations[0];
      L.circleMarker([s.lat, s.lon], { radius: 8, color: "#1d232a", weight: 3, fillColor: "#ffd60a", fillOpacity: 1 })
        .bindTooltip("Change: " + s.name_en).addTo(routeLayer);
    });
    for (const id of [r.from.id, r.to.id]) markers[id].bringToFront();
    map.fitBounds(L.latLngBounds(pts), { padding: [40, 40], maxZoom: 14, animate: false });

    const h = document.createElement("p");
    h.className = "summary";
    h.innerHTML = `About ${r.total_minutes} min <small>${r.transfers} change${r.transfers === 1 ? "" : "s"} of line` +
      (r.transfers ? ` (incl. ${minutes} min per change)` : "") + "</small>";
    const steps = document.createElement("ol");
    steps.className = "steps";
    r.legs.forEach((leg, i) => {
      if (i > 0) {
        const c = document.createElement("li");
        c.className = "change";
        c.textContent = `Change at ${leg.stations[0].name_en} to ${leg.name}`;
        steps.append(c);
      }
      const li = document.createElement("li");
      li.style.setProperty("--c", leg.color);
      const first = leg.stations[0], last = leg.stations[leg.stations.length - 1];
      const badge = document.createElement("span");
      badge.className = "badge";
      badge.style.background = leg.color;
      badge.textContent = leg.line;
      const text = document.createElement("span");
      text.innerHTML = "";
      text.append(`${first.name_en} → ${last.name_en}`);
      const meta = document.createElement("div");
      meta.className = "fa";
      meta.textContent = `towards ${towards(leg.line, first.id, last.id)} · ${leg.stops} stop${leg.stops === 1 ? "" : "s"} · ${Math.round(leg.seconds / 60)} min`;
      const det = document.createElement("details");
      const sum = document.createElement("summary");
      sum.textContent = "Stations";
      const list = document.createElement("ol");
      leg.stations.forEach((s) => {
        const it = document.createElement("li");
        it.textContent = `${s.name_en} · ${s.name_fa}`;
        list.append(it);
      });
      det.append(sum, list);
      li.append(badge, text, meta, det);
      steps.append(li);
    });
    out.append(h, steps);
  }

  function readHash() {
    const p = new URLSearchParams(location.hash.slice(1));
    if (p.get("change")) { $("penalty").value = p.get("change"); $("penalty-val").textContent = p.get("change") + " min"; }
    if (byId[p.get("from")] && byId[p.get("to")]) {
      picks = { from: p.get("from"), to: p.get("to") };
      $("from").value = label(byId[picks.from]);
      $("to").value = label(byId[picks.to]);
      findRoute();
    }
  }

  // ---- events -------------------------------------------------------------

  $("form").addEventListener("submit", (e) => { e.preventDefault(); findRoute(); });
  $("swap").addEventListener("click", () => {
    [picks.from, picks.to] = [picks.to, picks.from];
    [$("from").value, $("to").value] = [$("to").value, $("from").value];
    if ($("from").value && $("to").value) findRoute();
  });
  $("penalty").addEventListener("input", (e) => {
    $("penalty-val").textContent = e.target.value + " min";
    if (picks.from && picks.to) findRoute();
  });
  $("from").addEventListener("input", () => (picks.from = null));
  $("to").addEventListener("input", () => (picks.to = null));

  boot();
})();
