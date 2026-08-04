<!-- generated documentation — edit the source, not this file -->
# `bot/src/twin.ts`

@file Runs the compiled woz_uwb digital twin inside this Worker.
`web-twin/twin.js` embeds its WASM as a decoded byte string and instantiates
it at runtime with `WebAssembly.instantiate(bytes, imports)` — the one path
workerd refuses ("Wasm code generation disallowed by embedder", proven
directly against `wrangler dev`; see docs/twin-worker-phase0.md). twin.js
itself is never edited: Emscripten's `Module["instantiateWasm"]` hook lets
this file hand it a build-time-precompiled `WebAssembly.Module` instead
(imported as a static `.wasm` module, the one WASM path workerd does
allow), so twin.js's own embedded-bytes loader is never reached.
twin.wasm is extracted once by scripts/twin-wasm-extract.ts and drift-
checked against web-twin/twin.js by test/twin-wasm-drift.test.ts on every
run — a rebuilt twin.js with no matching re-extraction fails that gate
rather than silently running stale firmware.
The `.wasm` import below is deliberately dynamic, not a static top-level
import: Node's own module loader treats a static `import x from "*.wasm"`
as a native WASM-ES-module and tries to resolve twin.wasm's own imports
(`wasi_snapshot_preview1`) as JS packages, which crashes immediately under
plain `node --test` (this repo's own test runner) — a real regression
caught by running the full bot suite, not something wrangler's bundler
does. A dynamic `import()` is resolved lazily, only in the branch that
actually calls it, so Node never touches the WASM path at all.

**depends on** [`bot/src/citations.ts`](citations.ts.md)  ·  **used by** [`bot/src/commands/twin.ts`](../bot.src.commands/twin.ts.md)

## API

### `async function loadTwinWasmModule(): Promise<WebAssembly.Module>`
`bot/src/twin.ts:41`

Under Node (tests, scripts/twin-wasm-extract.ts's own consumers, this
repo's `node --test` runner): read the same bytes from disk and compile
them the ordinary way — Node allows runtime WASM codegen; only workerd
refuses it (docs/twin-worker-phase0.md).
Under workerd: `import("./twin.wasm")` must stay a literal,
statically-analyzable specifier so wrangler's bundler compiles it into a
`WebAssembly.Module` ahead of time — the one WASM path workerd allows.

**called by** `bootTwin`

### `export async function bootTwin(): Promise<TwinHandle>`
`bot/src/twin.ts:89`

Boot one twin instance. Each call is a fresh WASM instance (a firmware reboot).

**called by** `handleApproach`  ·  **calls** `cmOrNull`, `loadTwinWasmModule`

### `function applyNoise(cm: number, noise: NoiseLevel, rand: () => number): number`
`bot/src/twin.ts:174`

A deterministic bench-style noise model, not a firmware constant: SIM,
matching the calibration web-twin/index.html's own noise checkbox uses
(index.html:833-848 — "bench-like spikes", jitter ~+-6cm, ~6% chance of a
~1500-1600mm spike, cited there against app_main.cpp:237-238's observed
bench swing). "heavy" scales both knobs; there is no firmware source for
that scaling, it is this command's own choice of a rougher bench.

**called by** `runApproachScenario`

### `function makeRng(seed: number): () => number`
`bot/src/twin.ts:186`

xorshift32 — deterministic so a scenario's ASCII diagram/PNG can be reproduced
from the same options without storing the whole round list.

**called by** `runApproachScenario`

<details><summary>Undocumented (6)</summary>

- `cmOrNull`
- `instantiateWasm`
- `ScenarioTooLong`
- `ScenarioTooLong.constructor`
- `runApproachScenario` — tested: it:a dropped round carries a null measured cm and does not call into the firmware for it@l55; it:every round is block ms apart and the round count matches the reported length@l46; it:explains a spread-break from a noise spike when trust does not complete@l22; it:reaches trust with no noise and no drops@l13; it:throws scenario too long, naming the round count, past the cap@l33
- `explainDecision`

</details>
