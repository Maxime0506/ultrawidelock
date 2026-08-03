/* Regenerate activity/twin.lock.json from the current web-twin/ sources.
 *
 * Run this only when the twin was rebuilt on purpose (`make twin-wasm`). The
 * lock exists so that a changed firmware blob has to pass through a reviewed
 * commit instead of riding along in a deploy, so refreshing it without looking
 * at what changed defeats the point. */

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const TWIN = resolve(here, "..", "..", "web-twin");
const LOCK = resolve(here, "..", "twin.lock.json");

const existing = JSON.parse(readFileSync(LOCK, "utf8"));
const files = {};

for (const name of Object.keys(existing.files)) {
  const buf = readFileSync(resolve(TWIN, name));
  files[name] = {
    bytes: buf.byteLength,
    sha256: createHash("sha256").update(buf).digest("hex"),
  };
  const was = existing.files[name];
  const changed = was.bytes !== files[name].bytes || was.sha256 !== files[name].sha256;
  console.log(`  ${changed ? "CHANGED" : "same   "}  ${name}  ${files[name].bytes} B  ${files[name].sha256}`);
}

writeFileSync(LOCK, JSON.stringify({ ...existing, files }, null, 2) + "\n");
console.log(`\nwrote ${LOCK}`);
