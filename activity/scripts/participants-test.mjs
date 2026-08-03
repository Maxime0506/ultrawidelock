/* Hostile-input test for the "N watching" strip.
 *
 * Usernames are strings other people chose, and Discord's docs say not to
 * trust what the SDK reports client-side. This drives src/participants.ts with
 * names designed to break out of the strip, in a real browser DOM, and asserts
 * that none of them do. Run: node scripts/participants-test.mjs
 *
 * The strip is bundled with esbuild and handed a duck-typed SDK, so the code
 * under test is the real module rather than a copy of its logic.
 */

import { execFileSync, spawn } from "node:child_process";
import http from "node:http";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(here, "..");

/* Fixed list rather than argv or the environment: nothing an outer process
 * controls may reach an exec. Same reasoning as web-twin/csp_probe.py. */
const FIREFOX_CANDIDATES = [
  "/Applications/Firefox.app/Contents/MacOS/firefox",
  "/usr/bin/firefox",
  "/usr/local/bin/firefox",
  "/snap/bin/firefox",
];
const FIREFOX = FIREFOX_CANDIDATES.find((p) => {
  try {
    return readFileSync(p) && true;
  } catch {
    return false;
  }
});
if (!FIREFOX) {
  console.error("participants-test: no Firefox in " + FIREFOX_CANDIDATES.join(", "));
  process.exit(2);
}

const tmp = mkdtempSync(resolve(tmpdir(), "oa-ptest-"));
const bundle = resolve(tmp, "participants.mjs");
execFileSync(resolve(ROOT, "node_modules/.bin/esbuild"), [
  resolve(ROOT, "src/participants.ts"),
  "--bundle",
  "--format=esm",
  "--platform=browser",
  `--outfile=${bundle}`,
  "--log-level=error",
]);

const PAGE = `<!doctype html><meta charset="utf-8"><title>t</title>
<body>
<div class="topbar"><span class="sub">x</span><button id="themeBtn">t</button></div>
<script type="module">
import { startParticipants } from "./participants.mjs";

const HOSTILE = [
  { username: '<img src=x onerror="window.__pwned=1">' },
  { username: 'a\\u202Egnp.txt' },                       // bidi override
  { username: 'z\\u200B\\u200B\\u200B\\u200Bhidden' },       // zero-width padding
  { username: 'x'.repeat(300) },                        // overlong
  { username: '' },                                     // empty
  { username: '</span><script>window.__pwned=2<\\/script>' },
  { username: 'ok name' },
];

const sdk = {
  commands: { getInstanceConnectedParticipants: async () => ({ participants: HOSTILE }) },
  subscribe: async () => {},
};

const results = [];
const ok = (name, cond, detail) => results.push({ name, pass: !!cond, detail: detail ?? "" });

await startParticipants(sdk);
await new Promise((r) => setTimeout(r, 250));

const el = document.querySelector(".oa-watching");
ok("strip.mounted", !!el);
ok("strip.shown", el && el.hasAttribute("data-shown"));

// Nothing user-controlled may become an element.
ok("no.img", document.querySelectorAll(".oa-watching img").length === 0);
ok("no.script", document.querySelectorAll(".oa-watching script").length === 0);
ok("no.injected.elements", el ? el.querySelectorAll("*").length <= 4 : false,
   el ? "children=" + el.querySelectorAll("*").length : "");
ok("no.execution", window.__pwned === undefined, "pwned=" + window.__pwned);

const text = el ? el.textContent : "";
// The markup survives as literal text, which is the proof it was escaped.
ok("markup.is.text", text.includes("<img src=x"), text.slice(0, 60));
// Deceptive characters are gone.
ok("no.bidi", !/[\\u202A-\\u202E\\u2066-\\u2069]/.test(text));
ok("no.zerowidth", !/[\\u200B-\\u200F\\uFEFF]/.test(text));
ok("no.control", !/[\\u0000-\\u001F\\u007F]/.test(text));
// Length clamp held, and the count is the full list not the shown subset.
ok("clamped", !/x{40}/.test(text), text.slice(0, 60));
ok("count.is.total", text.includes("7"), text.slice(0, 40));
ok("overflow.marker", text.includes("+4"), text.slice(0, 60));

navigator.sendBeacon("/report", JSON.stringify(results));
<\/script>
</body>`;

const results = await new Promise((done) => {
  const srv = http.createServer((req, res) => {
    if (req.method === "POST" && req.url === "/report") {
      let body = "";
      req.on("data", (c) => (body += c));
      req.on("end", () => {
        res.writeHead(204).end();
        srv.close();
        done(JSON.parse(body || "[]"));
      });
      return;
    }
    if (req.url === "/" || req.url === "/index.html") {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" }).end(PAGE);
      return;
    }
    if (req.url === "/participants.mjs") {
      res.writeHead(200, { "Content-Type": "text/javascript; charset=utf-8" })
        .end(readFileSync(bundle));
      return;
    }
    res.writeHead(404).end();
  });

  srv.listen(0, "127.0.0.1", () => {
    const port = srv.address().port;
    const prof = mkdtempSync(resolve(tmpdir(), "oa-ff-"));
    const p = spawn(FIREFOX, ["--headless", "--profile", prof, "--no-remote",
                              `http://127.0.0.1:${port}/`],
                    { stdio: "ignore" });
    /* A browser that never reports must fail the run rather than hang it. */
    const killAt = setTimeout(() => {
      srv.close();
      done([]);
    }, 60000);
    srv.on("close", () => {
      clearTimeout(killAt);
      p.kill();
      rmSync(prof, { recursive: true, force: true });
    });
  });
});

rmSync(tmp, { recursive: true, force: true });

if (!results.length) {
  console.error("participants-test: browser never reported");
  process.exit(1);
}
const fails = results.filter((r) => !r.pass);
for (const r of results) {
  console.log(`  ${r.pass ? "ok  " : "FAIL"} ${r.name}${r.detail ? "  " + r.detail : ""}`);
}
if (fails.length) {
  console.error(`\nparticipants: ${fails.length}/${results.length} FAILED`);
  process.exit(1);
}
console.log(`\nparticipants: ${results.length}/${results.length} pass (hostile names, real DOM)`);
