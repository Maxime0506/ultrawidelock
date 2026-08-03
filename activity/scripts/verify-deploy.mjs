/* Check that what a host actually serves is what we built.
 *
 * A CDN is entitled to compress, cache and rewrite. twin.js is a binary file
 * wearing a .js extension, so a host that "helpfully" minified or re-encoded it
 * would corrupt the firmware while still returning 200 and looking fine in a
 * browser tab. This fetches the deployed files and compares them byte for byte
 * against the local build, and reports the response headers so an injected CSP
 * cannot arrive unnoticed.
 *
 * Usage: node scripts/verify-deploy.mjs https://your-host.example
 */

import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const DIST = resolve(here, "..", "dist");
const TWIN = resolve(here, "..", "..", "web-twin");

const base = process.argv[2]?.replace(/\/+$/, "");
if (!base) {
  console.error("usage: node scripts/verify-deploy.mjs https://your-host.example");
  process.exit(2);
}

/* Compared against the built copy for index.html, because that one legitimately
 * carries the injected boot tag, and against the pristine twin for twin.js,
 * which must never differ from what the firmware build produced. */
/* Cloudflare Pages canonicalises away the .html suffix: /privacy.html 308s to
 * /privacy. The extensionless form is therefore what goes in the Discord portal
 * and what is checked here, because a 308 has an empty body and would pass a
 * naive status check while serving nothing. */
const CHECKS = [
  { path: "/index.html", local: resolve(DIST, "index.html"), against: "dist/index.html" },
  { path: "/twin.js", local: resolve(TWIN, "twin.js"), against: "web-twin/twin.js" },
  { path: "/discord-boot.js", local: resolve(DIST, "discord-boot.js"), against: "dist/discord-boot.js" },
  { path: "/privacy", local: resolve(DIST, "privacy.html"), against: "dist/privacy.html" },
  { path: "/terms", local: resolve(DIST, "terms.html"), against: "dist/terms.html" },
];

/* Headers worth seeing on every deploy. A CSP appearing here would change what
 * the twin is allowed to do, and phase 0 showed it needs both 'wasm-unsafe-eval'
 * and 'unsafe-inline' to run at all. */
const SHOW = ["content-type", "content-encoding", "content-security-policy", "x-frame-options"];

let bad = 0;

for (const { path, local, against } of CHECKS) {
  const url = base + path;
  let res;
  try {
    res = await fetch(url);
  } catch (e) {
    console.log(`  FAIL ${path}: ${e.message}`);
    bad++;
    continue;
  }

  if (!res.ok) {
    console.log(`  FAIL ${path}: HTTP ${res.status}`);
    bad++;
    continue;
  }

  const served = Buffer.from(await res.arrayBuffer());
  const want = readFileSync(local);
  const same = served.equals(want);
  if (!same) bad++;

  console.log(`  ${same ? "ok  " : "FAIL"} ${path}  ${served.byteLength} B vs ${against} ${want.byteLength} B`);
  for (const h of SHOW) {
    const v = res.headers.get(h);
    if (v) console.log(`         ${h}: ${v}`);
  }
}

if (bad) {
  console.log(`\n${bad} check(s) FAILED: the host is not serving what was built.`);
  process.exit(1);
}
console.log(`\nall ${CHECKS.length} files served byte-identical by ${base}`);
