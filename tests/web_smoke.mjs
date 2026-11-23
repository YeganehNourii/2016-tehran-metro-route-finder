// Smoke test for the WebAssembly build (run by the Pages workflow):
// loads site/metro.js in Node and asks for real routes.
import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const require = createRequire(import.meta.url);
const createMetroModule = require("../site/metro.js");
const M = await createMetroModule();
const init = M.cwrap("mt_init", "number", ["string"]);
const route = M.cwrap("mt_route", "string", ["string", "string", "number"]);
const search = M.cwrap("mt_search", "string", ["string", "number"]);

const n = init(readFileSync(new URL("../site/data/tehran_metro.json", import.meta.url), "utf8"));
assert.ok(n > 100, `expected >100 stations, got ${n}`);

const direct = JSON.parse(route("Tajrish", "Imam Khomeini", 5));
assert.equal(direct.ok, true);
assert.equal(direct.transfers, 0);
assert.equal(direct.legs[0].line, "1");

const change = JSON.parse(route("tajrish", "azadi", 5));
assert.ok(change.transfers >= 1);

assert.equal(JSON.parse(route("tajrish", "qqqzzz", 5)).ok, false);
assert.equal(JSON.parse(search("تجریش", 1))[0].name_en, "Tajrish");
console.log(`web_smoke: ok (${n} stations; Tajrish → Azadi ${change.total_minutes} min)`);
