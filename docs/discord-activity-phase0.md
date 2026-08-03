# Discord Activity, phase 0: does the twin's WASM survive a sandbox CSP?

**Answer: yes. Confirmed in a live Discord Activity on 2026-08-03, 22/22 self-test and a
full walk-up to BOLT_DRIVEN. Phase 1 is unblocked.**

Record of the spike that gates the whole "run `web-twin/` as a Discord Activity" idea.
Written against `8d6ede1`. Reproduce the local half with `web-twin/csp_probe.py`.

## The question

`web-twin/twin.js` does not fetch a `.wasm` file. It carries the module inline as a byte
array and hands it to `WebAssembly.instantiate(binary, imports)`:

```
web-twin/twin.js  var wasmBinaryFile; function findWasmBinary(){return binaryDecode('\0asm\1\0\0\0…
web-twin/twin.js  async function instantiateArrayBuffer(binaryFile,imports){
                    var binary=await getWasmBinary(binaryFile);
                    var instance=await WebAssembly.instantiate(binary,imports); …
```

That sidesteps MIME type and proxy concerns (the page makes zero runtime network
requests), but it puts the whole twin on the buffer-compilation path, which a
Content-Security-Policy gates behind `'wasm-unsafe-eval'`. A Discord Activity runs in a
sandboxed iframe. If that iframe's CSP omits the directive, the firmware never boots and
the rest of the port is wasted work.

## What was actually run

Local only. `web-twin/csp_probe.py` serves `web-twin/` with a chosen CSP header, loads it
inside an iframe in headless Firefox 153.0.1 (the same document shape Discord uses), and
reads the page's own `#selftest` element plus any `securitypolicyviolation` events back
out. The page stamps that element with class `pass`/`fail` when the run finishes, so the
probe waits on the page's own signal rather than on a guess at its wording.

```
$ python3 web-twin/csp_probe.py web-twin

== baseline-no-csp
   sanity: proves the harness itself works
   CSP: (none)
   status:   settled:pass
   selftest: firmware self-test: 22/22 vs test_twin.c + test_approach.c (WASM)

== no-wasm-unsafe-eval
   CSP: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'
   status:   settled:fail
   selftest: firmware self-test: FAIL firmware.loaded
   VIOLATION: script-src <- wasm-eval
   VIOLATION: script-src <- wasm-eval

== with-wasm-unsafe-eval
   CSP: default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'
   status:   settled:pass
   selftest: firmware self-test: 22/22 vs test_twin.c + test_approach.c (WASM)

== no-unsafe-inline
   CSP: default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'
   status:   timeout
   selftest: firmware self-test: …
   VIOLATION: script-src-elem <- inline
```

## Findings

1. **The twin needs `'wasm-unsafe-eval'`.** Without it the firmware does not load: the
   self-test settles on `FAIL firmware.loaded` and the browser raises
   `script-src <- wasm-eval`. This is the failure mode phase 0 was written to look for,
   and it is real rather than theoretical.

2. **The twin also needs `'unsafe-inline'`, which the plan did not anticipate.**
   `web-twin/index.html` carries all of its own logic in inline `<script>` blocks, so a
   policy that grants `'wasm-unsafe-eval'` but withholds `'unsafe-inline'` is *worse*
   than the first failure: the self-test never runs at all. The element is still showing
   its placeholder `firmware self-test: …` when the probe gives up, and the only evidence
   is `script-src-elem <- inline`. A reviewer watching for a red FAIL would see nothing
   and might call it a load timeout. Both directives are required; check for both.

3. **Given both directives, the inline-embedded module instantiates normally** under an
   otherwise strict `default-src 'self'` policy, inside an iframe, with 22/22 self-test
   checks passing. Nothing about the inline-byte-array approach is inherently hostile to
   a sandboxed frame.

## The tunnel path, measured

Serving `web-twin/` on `127.0.0.1:5173` behind `cloudflared tunnel` (2026.7.3), then
loading the resulting `https://<name>.trycloudflare.com/index.html` in headless Firefox:

- **Cloudflare injects no CSP.** The full response header set on `index.html` is
  `date, content-type, cf-ray, cf-cache-status, last-modified, server`. No
  `content-security-policy`, no `x-frame-options`. At the tunnel layer, policy is
  whatever the origin sends, which is nothing. Anything restrictive seen inside Discord
  therefore comes from Discord, not from the tunnel.
- **`twin.js` survives byte-identical.** `cmp` against the repo copy is clean at
  37,465 B, and it is served as `text/javascript`. Worth checking explicitly because the
  file is binary; a proxy that transcoded or re-encoded it would corrupt the embedded
  module.
- **The twin boots and self-tests green over HTTPS**, footer reading
  `firmware self-test: 22/22 vs test_twin.c + test_approach.c (WASM)`.
- **The page requests exactly two URLs**, `/index.html` and `/twin.js`. The server log
  shows nothing else, not even a favicon. The "zero runtime network requests" claim is
  confirmed rather than assumed, which is what makes the URL-mapping story trivial: one
  root mapping covers the whole app.

## Result: green, confirmed in a real Discord Activity

Run 2026-08-03 on the macOS desktop client, app launched from the Developer Activity
Shelf into a voice channel, URL Mapping `/` → the quick tunnel above.

- Footer: **`firmware self-test: 22/22 vs test_twin.c + test_approach.c (WASM)`**, green.
- A full walk-up completed: every phase chip lit through `BOLT_DRIVEN`, trust
  `3/3` on layer 4, `BOLT UNLOCKED`, `WALLET Unsecured`, last latched range 68 cm
  against 67 cm true, with the firmware debugger panel tracking the
  Pre-POLL/POLL/Response/Final state machine.

So Discord's Activity iframe grants **both** `'wasm-unsafe-eval'` and `'unsafe-inline'`.
The inline-embedded module instantiates, and the CCC DS-TWR responder runs unmodified
inside the sandbox. **The fallback project, same-origin `.wasm` plus an Emscripten
relink, is not needed.**

Everything else on the checklist has since been closed:

- **Single-step mode and the theme toggle under blocked storage** are verified by
  `activity/scripts/iframe-checks.py`, which drives the twin in a same-origin iframe with
  `dom.storage.enabled=false` in the browser profile, so the page takes the real
  exception path its try/catch was written for. 12 checks: the theme still flips
  (`light -> dark`) with `localStorage` throwing, and stepping cycles all five legs,
  `POLL -> RESP TX -> FINAL -> FINAL_DATA -> PRE-POLL`, with the self-test intact
  throughout.
- **Mobile** was confirmed on a physical handset: dragging and the sliders work, and the
  drag does not fight the client's own dismiss gesture.
- The **devtools console** was not read directly, and on reflection there is little there
  to read. The page makes exactly two requests and no others, so the usual CSP casualties
  do not exist, and the two things that could have been refused, WASM compilation and
  inline scripts, are both proven working by the self-test passing at all. Recorded as a
  judgement rather than a verification.

## Getting an unpublished Activity to appear at all

This cost more rounds than the actual spike, so it is worth writing down. An undistributed
Activity is launched from the **Developer Activity Shelf**; ownership is enough, and the
app does not need to be installed to a guild or published. Three settings gate it, and
missing any one produces the identical symptom, `No activities match your search`,
which makes it undiagnosable by symptom alone:

1. Discord → User Settings → Advanced → **Developer Mode** on.
2. Portal → Activities → Settings → **Enable Activities** on.
3. Portal → Activities → Settings → **Supported Platforms**: tick the platform.

Trap in step 3: the platform list is **`web` / `ios` / `android` with no desktop entry**.
The desktop client renders Activities in an embedded web view, so **`web` is what makes
it appear on desktop**. Ticking `web` was the fix here.

`Application Test Mode` in the same Advanced settings pane is unrelated: it simulates
purchases and SKUs for monetised apps. Leave it off.

## What this does *not* settle

The exact CSP Discord serves on `<client_id>.discordsays.com` was never read directly.
The launch proves the two directives we care about are granted, not what the full policy
says. If a later phase adds a subresource, a font, or a `connect-src` target, that is a
new question and needs its own check rather than an appeal to this result.

## How to close phase 0

Everything below the portal is already proven to work; only steps 1 and 4 need a Discord
account.

1. Developer Portal → new app → Activities → **Enable Activities** (this auto-creates the
   `Launch` Entry Point command).
2. `python3 -m http.server 5173 --bind 127.0.0.1` from inside `web-twin/`, then
   `cloudflared tunnel --url http://localhost:5173 --no-autoupdate`.
3. URL Mapping: `/` → the `<name>.trycloudflare.com` host, with no path prefix. A quick
   tunnel's hostname is regenerated on every run, so this mapping has to be re-edited
   each session; that is a dev-loop annoyance only, and phase 2 replaces it with a stable
   host.
4. Launch in a voice channel and read the footer. Three outcomes:
   - `22/22 vs test_twin.c + test_approach.c (WASM)` → green, proceed to phase 1.
   - `FAIL firmware.loaded` → `'wasm-unsafe-eval'` is missing. Stop.
   - still `firmware self-test: …` → inline scripts are blocked. Stop.
5. Capture the console either way and append it here.

Outcomes 2 and 3 both mean the fallback is a same-origin `.wasm` file with correct
headers and an Emscripten relink, which is a different project and out of scope for
this one.

## Corrections to the phase-0 brief

- The brief says the footer "replay[s] the scenario asserted by `tests/host/test_twin.c`".
  It replays more than that: 22 checks against `test_twin.c` **and** `test_approach.c`
  (`web-twin/index.html:745`). The node-side gate `web-twin/selftest.cjs` is the smaller
  one at 18 checks. Both pass on `8d6ede1`.
- `web-twin/twin.js` is a binary file (`file` reports `data`; 3,076 NUL bytes). BSD `grep`
  silently reports no match on it and exits 1, so the brief's `findWasmBinary` quote
  cannot be confirmed with `grep`. Use Python. This will bite anyone auditing the file.

## Constraints this phase confirmed for later ones

- `make security GATES="web"` scopes `web-twin/` and carries a deliberate baseline entry
  `csp:web-twin/index.html  # no CSP` (`security/web-baseline.txt:29`). That gate fails
  when a baseline line **stops** matching, so adding a `<meta>` CSP to the twin would
  break the build. Another reason the Activity must set policy at the HTTP layer and
  leave the page alone.
- Baselines on `8d6ede1`, all green before any change: `make test` 3733 checks,
  `check_constants.py` 16/16, `selftest.cjs` 18/18, `make security GATES="web"` pass.
