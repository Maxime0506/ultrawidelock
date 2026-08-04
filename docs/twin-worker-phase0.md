# Phase 0: can the WASM twin run inside a Cloudflare Worker?

Spike for the `/twin` Discord command design (build-on-demand bot, prompt 3, part B).
Question: can `web-twin/twin.js`'s inline-embedded WASM be instantiated and run inside
a real Cloudflare Workers runtime (`workerd`), so `/twin` executes the actual firmware
rather than modelling it? Answer recorded here per that prompt's own requirement:
"Report the finding before building anything on top of it."

## Method

Real `workerd` via `wrangler dev` (wrangler 4.118.0, workerd 1.20260730.1, installed
in an isolated scratch npm project, not added to this repo). `wrangler dev` runs the
same open-source runtime binary Cloudflare deploys, not a browser or a mock. Three
probe workers were written and run against `http://localhost:<port>/`; none of this
touched `web-twin/` or committed anything to the repo.

## Finding 1 — twin.js's own WASM load path is rejected by workerd

`twin.js` embeds the WASM module as a string, decoded at runtime by `binaryDecode()`
(twin.js:2784-ish) and handed to `WebAssembly.instantiate(bytes, imports)` inside its
own `instantiateArrayBuffer()`. Loading `twin.js` unmodified in a Worker and calling
`createTwin()` fails immediately:

```
RuntimeError: Aborted(CompileError: WebAssembly.instantiate(): Wasm code generation
disallowed by embedder)
```

Confirmed this is not specific to twin.js's WASM (size, imports, or content): the same
failure reproduces with a hand-built, spec-minimal 8-byte empty WASM module
(`00 61 73 6d 01 00 00 00`) through all three JS compile entry points —
`new WebAssembly.Module(bytes)`, `WebAssembly.instantiate(bytes, {})`, and
`WebAssembly.compile(bytes)` — each returns the identical `CompileError`. This is a
blanket `workerd` embedder policy against compiling WASM from a runtime byte array,
the same class of restriction as disallowing `eval`/`new Function`. No compatibility
flag lifts it.

**Consequence:** `twin.js` cannot be dropped into a Worker and self-instantiate as it
does in a browser or Node. The embedded-bytes-as-JS-string load path is fundamentally
incompatible with `workerd`'s isolate security model, independent of plan tier.

## Finding 2 — the Emscripten `instantiateWasm` hook works around it, without touching twin.js

`twin.js`'s `createWasm()` checks `Module["instantiateWasm"]` before it ever calls its
own byte-decode path:

```js
var instantiateWasm=Module["instantiateWasm"];
if(instantiateWasm){return new Promise(resolve=>{instantiateWasm(info,inst=>res...
```

This is Emscripten's documented instantiation override, part of `twin.js`'s existing
public call surface — using it is not a fork, bundle, minify, or transform of the file.
Cloudflare Workers **do** allow WASM that is compiled ahead of time and imported as a
build-time module resource (`import wasmModule from "./twin.wasm"`); the restriction in
Finding 1 is specifically against *runtime* compilation from bytes inside the isolate.

Verified end to end:

1. Captured the exact embedded WASM bytes by monkey-patching `WebAssembly.instantiate`
   in Node before calling `createTwin()` from the real, unmodified `twin.js` — i.e. the
   same bytes `twin.js`'s own `binaryDecode()` produces, extracted without editing or
   re-encoding the file. Result: 25,896 bytes, sha256
   `a5bd6bf2bb33a4a212ff6064cf87c25a66b88cae33e56d7c3a636d2aa52084f8`, valid WASM (magic
   `\0asm`, version 1).
2. Saved that as `twin.wasm`, imported it into a Worker (`import wasmModule from
   "./twin.wasm"`), and supplied `instantiateWasm(imports, successCallback) =>
   WebAssembly.instantiate(wasmModule, imports).then(i => successCallback(i,
   wasmModule))` as a `createTwin()` module arg. `twin.js` itself: zero edits.
3. Ran the exact scenario `tests/host/test_twin.c` asserts (the same 18 checks
   `web-twin/selftest.cjs` already mirrors: boot, one honest 234 cm block latches and is
   not yet trusted, a Ghost-Peak −400 spoof block doesn't reduce range/doesn't get
   trusted/resets trust to 0/doesn't wake the latch, three agreeing blocks (120/122/121)
   earn trust at K, and the per-leg `_twin_step` stepper wraps a full DS-TWR leg)
   against this Worker-hosted instance.

**Result: 18/18 PASS**, inside real `workerd`. Timings: instantiate 2 ms, scenario 5 ms,
total 7 ms.

## Finding 3 — CPU budget is not a real constraint at realistic scenario sizes

Stress-tested `_twin_step` in the same Worker: 500 rounds / 6 ms, 5,000 rounds / 36 ms,
20,000 rounds / 139 ms — roughly 7-12 µs/round. Cloudflare's published CPU-time limits
(`developers.cloudflare.com/workers/platform/limits/`, fetched directly): Free plan
10 ms/request, Paid plan 30 s default (configurable to 5 min). A realistic `/twin
approach` scenario (tens to low hundreds of DS-TWR rounds for one walk-up) costs under
2 ms — trivial even against the Free-plan ceiling, and irrelevant on Paid. Global-scope
instantiation (recommended, so warm requests skip it) costs ~2 ms against the separate
1-second global-scope startup budget.

The scenario-length cap called for in the parent prompt's Robustness item 4 is still
worth implementing, but as defense-in-depth against a pathological `/twin explain`
input, not because CPU time is actually tight.

## Verdict

**Phase 0: yes, with one required design change.** The twin runs for real inside
`workerd` and reproduces `test_twin.c`'s scenario exactly. But it cannot be loaded as a
bare `import createTwin from "./twin.js"` and left to self-instantiate — every `/twin`
implementation must supply the `instantiateWasm` hook and a companion `twin.wasm`
extracted from `twin.js`'s embedded bytes.

Follow-up for Part B implementation (not resolved here):

- `twin.wasm` must be checked into the repo as a build artifact with a byte/hash drift
  guard against `twin.js`'s embedded blob (mirrors `web-twin/check_constants.py`'s
  existing role and the `twin.lock.json` pattern used for the unrelated Discord-Activity
  approach), so a rebuilt twin arrives as a reviewed diff, not silent drift.
- Confirm whether a deployed (not just `wrangler dev`) Worker needs an explicit
  `[[rules]]`/`wasm_modules` entry in `wrangler.toml` for the `.wasm` import — local dev
  resolved it with zero config, not yet confirmed against a real deployment.
- This spike used a scratch npm project outside the repo (`wrangler`/`workerd` are not
  added as dependencies here); the bot's actual `wrangler.toml`/`package.json` are Part
  A/B implementation work, not part of this spike.

## Caveat

`wrangler dev` runs the real open-source `workerd` binary, not an emulator, and the
error text ("Wasm code generation disallowed by embedder") is a V8-embedder-level
message rather than a wrangler-specific one — both point at this being representative
of production. No deploy to an actual Cloudflare account was performed to confirm this
directly; that remains open until Part B ships a real deployment.
