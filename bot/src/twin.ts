/**
 * @file Runs the compiled woz_uwb digital twin inside this Worker.
 *
 * `web-twin/twin.js` embeds its WASM as a decoded byte string and instantiates
 * it at runtime with `WebAssembly.instantiate(bytes, imports)` — the one path
 * workerd refuses ("Wasm code generation disallowed by embedder", proven
 * directly against `wrangler dev`; see docs/twin-worker-phase0.md). twin.js
 * itself is never edited: Emscripten's `Module["instantiateWasm"]` hook lets
 * this file hand it a build-time-precompiled `WebAssembly.Module` instead
 * (imported as a static `.wasm` module, the one WASM path workerd does
 * allow), so twin.js's own embedded-bytes loader is never reached.
 *
 * twin.wasm is extracted once by scripts/twin-wasm-extract.ts and drift-
 * checked against web-twin/twin.js by test/twin-wasm-drift.test.ts on every
 * run — a rebuilt twin.js with no matching re-extraction fails that gate
 * rather than silently running stale firmware.
 *
 * The `.wasm` import below is deliberately dynamic, not a static top-level
 * import: Node's own module loader treats a static `import x from "*.wasm"`
 * as a native WASM-ES-module and tries to resolve twin.wasm's own imports
 * (`wasi_snapshot_preview1`) as JS packages, which crashes immediately under
 * plain `node --test` (this repo's own test runner) — a real regression
 * caught by running the full bot suite, not something wrangler's bundler
 * does. A dynamic `import()` is resolved lazily, only in the branch that
 * actually calls it, so Node never touches the WASM path at all.
 */
// Types resolved by twin-js.d.ts. twin.js itself is consumed as-is, unmodified.
import { cite, TWIN } from "./citations.ts";
import createTwin from "../../web-twin/twin.js";

/**
 * Under Node (tests, scripts/twin-wasm-extract.ts's own consumers, this
 * repo's `node --test` runner): read the same bytes from disk and compile
 * them the ordinary way — Node allows runtime WASM codegen; only workerd
 * refuses it (docs/twin-worker-phase0.md).
 *
 * Under workerd: `import("./twin.wasm")` must stay a literal,
 * statically-analyzable specifier so wrangler's bundler compiles it into a
 * `WebAssembly.Module` ahead of time — the one WASM path workerd allows.
 */
async function loadTwinWasmModule(): Promise<WebAssembly.Module> {
	if (typeof process !== "undefined" && process.versions?.node) {
		const { readFileSync } = await import("node:fs");
		const { join } = await import("node:path");
		const path = join(import.meta.dirname, "twin.wasm");
		// @cloudflare/workers-types declares WebAssembly.Module as abstract,
		// reflecting the real workerd restriction this file exists to work
		// around (docs/twin-worker-phase0.md) — but this branch only runs
		// under Node, where the ordinary constructor is exactly what's needed.
		const NodeWasmModule = WebAssembly.Module as unknown as new (bytes: Uint8Array) => WebAssembly.Module;
		return new NodeWasmModule(readFileSync(path));
	}
	const mod = (await import("./twin.wasm")) as { default: WebAssembly.Module };
	return mod.default;
}

/** twin_glue.c TWIN_NO_RANGE. */
const NO_RANGE = -100000;

/** One ranging block's cadence. Cited as TWIN.blockCadence, so the value is
 *  re-checked against the firmware line on every `npm run drift`. */
export const BLOCK_MS = 192;

/** Consecutive agreeing blocks to trust (TWIN.trustK). */
export const FIRA_RANGE_TRUST_K = 3;
/** Max block-to-block delta to stay agreed (TWIN.spreadCm). */
export const FIRA_RANGE_SPREAD_CM = 50;
/** Layer-1 plausibility envelope (TWIN.negTol, TWIN.maxCm). */
export const FIRA_RANGE_NEG_TOL_CM = 30;
export const FIRA_RANGE_MAX_CM = 3000;

export const LEGS = ["Pre-POLL", "POLL", "Response TX", "Final", "Final_Data"] as const;

export interface TwinHandle {
	block(cm: number): void;
	step(cm: number): number;
	leg(): number;
	lastCm(): number | null;
	trustedCm(): number | null;
	trustLevel(): number;
	trustK(): number;
}

function cmOrNull(v: number): number | null {
	return v === NO_RANGE ? null : v;
}

/** Boot one twin instance. Each call is a fresh WASM instance (a firmware reboot). */
export async function bootTwin(): Promise<TwinHandle> {
	const twinWasm = await loadTwinWasmModule();
	const m = await createTwin({
		print: () => {},
		instantiateWasm(
			imports: WebAssembly.Imports,
			successCallback: (instance: WebAssembly.Instance, module?: WebAssembly.Module) => void,
		) {
			WebAssembly.instantiate(twinWasm, imports).then((instance: WebAssembly.Instance) =>
				successCallback(instance, twinWasm),
			);
			return {};
		},
	});
	if (m._twin_boot() !== 0) throw new Error("twin_boot() returned nonzero");

	return {
		block: (cm: number) => m._twin_block(cm | 0),
		step: (cm: number) => m._twin_step(cm | 0),
		leg: () => m._twin_leg(),
		lastCm: () => cmOrNull(m._twin_last_cm()),
		trustedCm: () => cmOrNull(m._twin_trusted_cm()),
		trustLevel: () => m._twin_trust_level(),
		trustK: () => m._twin_trust_k(),
	};
}

export type NoiseLevel = "none" | "light" | "heavy";

export interface ApproachOptions {
	/** Walk speed in m/s. web-twin/index.html's own slider bounds: 0.2-2.5. */
	speedMps: number;
	noise: NoiseLevel;
	/** Ranging blocks that never arrive (radio drop), spread evenly through the walk. */
	drops: number;
	/** Starting distance in metres. Matches the twin page's BLE radius default. */
	startDistanceM?: number;
}

export interface RoundRecord {
	block: number;
	groundTruthCm: number;
	measuredCm: number | null; // null when this round was dropped
	trustLevelAfter: number;
	trustedCmAfter: number | null;
}

export interface ApproachResult {
	rounds: RoundRecord[];
	finalTrustedCm: number | null;
	finalTrustLevel: number;
	trustK: number;
	decision: {
		outcome: "trusted" | "not-trusted";
		reason: string;
		citation: string;
	};
	timingsMs: { instantiateMs: number; scenarioMs: number };
}

/** Hard cap independent of speed/distance, so a pathological input still costs a bounded
 *  number of _twin_block calls. Phase 0 measured ~7-12us/round in workerd (docs/twin-worker-phase0.md),
 *  so this is headroom, not a real CPU-time concern — the cap exists to give a caller a
 *  concrete, named limit rather than an unbounded loop. */
export const MAX_ROUNDS = 500;

export class ScenarioTooLong extends Error {
	readonly wouldBeRounds: number;
	constructor(wouldBeRounds: number) {
		super(
			`that scenario would take ${wouldBeRounds} ranging blocks; ` +
				`/twin approach caps a run at ${MAX_ROUNDS}. Raise the speed or shorten the walk.`,
		);
		this.wouldBeRounds = wouldBeRounds;
	}
}

/**
 * A deterministic bench-style noise model, not a firmware constant: SIM,
 * matching the calibration web-twin/index.html's own noise checkbox uses
 * (index.html:833-848 — "bench-like spikes", jitter ~+-6cm, ~6% chance of a
 * ~1500-1600mm spike, cited there against app_main.cpp:237-238's observed
 * bench swing). "heavy" scales both knobs; there is no firmware source for
 * that scaling, it is this command's own choice of a rougher bench.
 */
function applyNoise(cm: number, noise: NoiseLevel, rand: () => number): number {
	if (noise === "none") return cm;
	const jitterScale = noise === "heavy" ? 18 : 6;
	const spikeChance = noise === "heavy" ? 0.18 : 0.06;
	const spikeCm = noise === "heavy" ? 3000 : 1500;
	let out = cm + Math.round((rand() + rand() - 1) * jitterScale);
	if (rand() < spikeChance) out += spikeCm + Math.round(rand() * 100);
	return out;
}

/** xorshift32 — deterministic so a scenario's ASCII diagram/PNG can be reproduced
 *  from the same options without storing the whole round list. */
function makeRng(seed: number): () => number {
	let s = seed || 0x9e3779b9;
	return () => {
		s ^= s << 13;
		s ^= s >>> 17;
		s ^= s << 5;
		s >>>= 0;
		return s / 0xffffffff;
	};
}

export function runApproachScenario(handle: TwinHandle, opts: ApproachOptions, seed = 1): ApproachResult {
	const t0 = Date.now();
	const startCm = Math.round((opts.startDistanceM ?? 8) * 100);
	// cm covered per BLOCK_MS-spaced ranging block: speedMps [m/s] * BLOCK_MS [ms] / 10 -> cm.
	const stepCm = Math.max(1, Math.round((opts.speedMps * BLOCK_MS) / 10));
	const wouldBeRounds = Math.ceil(startCm / stepCm) + 1;
	if (wouldBeRounds > MAX_ROUNDS) throw new ScenarioTooLong(wouldBeRounds);

	const rand = makeRng(seed);
	const dropSet = new Set<number>();
	if (opts.drops > 0) {
		const totalRounds = Math.min(wouldBeRounds, MAX_ROUNDS);
		const stride = Math.max(1, Math.floor(totalRounds / (opts.drops + 1)));
		for (let i = 1; i <= opts.drops; i++) dropSet.add(Math.min(totalRounds - 1, i * stride));
	}

	const rounds: RoundRecord[] = [];
	let distanceCm = startCm;
	let block = 1;
	while (distanceCm > 0 && rounds.length < MAX_ROUNDS) {
		const groundTruthCm = distanceCm;
		const dropped = dropSet.has(block);
		if (!dropped) {
			const measuredCm = applyNoise(groundTruthCm, opts.noise, rand);
			handle.block(measuredCm);
			rounds.push({
				block,
				groundTruthCm,
				measuredCm,
				trustLevelAfter: handle.trustLevel(),
				trustedCmAfter: handle.trustedCm(),
			});
		} else {
			rounds.push({
				block,
				groundTruthCm,
				measuredCm: null,
				trustLevelAfter: handle.trustLevel(),
				trustedCmAfter: handle.trustedCm(),
			});
		}
		distanceCm -= stepCm;
		block++;
	}

	const scenarioMs = Date.now() - t0;
	const finalTrustedCm = handle.trustedCm();
	const finalTrustLevel = handle.trustLevel();
	const trustK = handle.trustK();

	const decision = explainDecision(rounds, finalTrustedCm, finalTrustLevel, trustK);

	return {
		rounds,
		finalTrustedCm,
		finalTrustLevel,
		trustK,
		decision,
		timingsMs: { instantiateMs: 0, scenarioMs },
	};
}

function explainDecision(
	rounds: RoundRecord[],
	finalTrustedCm: number | null,
	finalTrustLevel: number,
	trustK: number,
): ApproachResult["decision"] {
	if (finalTrustedCm !== null) {
		return {
			outcome: "trusted",
			reason:
				`${finalTrustLevel}/${trustK} consecutive agreeing blocks (within ` +
				`${FIRA_RANGE_SPREAD_CM} cm of each other) reached at block ${rounds.at(-1)?.block} — ` +
				`the layer-4 trust gate opened.`,
			citation: `${cite(TWIN.layer4Trust)}-199`,
		};
	}

	// Walk backward for the most recent event that kept trust from completing:
	// a dropped block, an implausible reading, or a spread break past the last
	// two measured, non-dropped rounds.
	const measured = rounds.filter((r) => r.measuredCm !== null) as (RoundRecord & { measuredCm: number })[];
	const lastDrop = [...rounds].reverse().find((r) => r.measuredCm === null);
	if (lastDrop && rounds.indexOf(lastDrop) >= rounds.length - trustK) {
		return {
			outcome: "not-trusted",
			reason:
				`block ${lastDrop.block} never arrived (simulated radio drop) within the last ` +
				`${trustK} blocks needed to complete a trust run.`,
			citation: `${cite(TWIN.layer4Trust)}-199`,
		};
	}

	for (const r of measured) {
		const implausible = r.measuredCm < -FIRA_RANGE_NEG_TOL_CM || r.measuredCm > FIRA_RANGE_MAX_CM;
		if (implausible) {
			return {
				outcome: "not-trusted",
				reason:
					`block ${r.block} measured ${r.measuredCm} cm, outside the plausible envelope ` +
					`[-${FIRA_RANGE_NEG_TOL_CM}, ${FIRA_RANGE_MAX_CM}] cm — layer 1 rejected it and trust reset to 0.`,
				citation: `${cite(TWIN.layer1Plausible)}-181`,
			};
		}
	}

	for (let i = 1; i < measured.length; i++) {
		const prev = measured[i - 1];
		const cur = measured[i];
		if (!prev || !cur) continue;
		const delta = Math.abs(cur.measuredCm - prev.measuredCm);
		if (delta > FIRA_RANGE_SPREAD_CM) {
			return {
				outcome: "not-trusted",
				reason:
					`block ${cur.block} measured ${cur.measuredCm} cm, ${delta} cm from the previous ` +
					`block — past the ${FIRA_RANGE_SPREAD_CM} cm agreement spread, so the trust run restarted.`,
				citation: `${cite(TWIN.layer4Trust)}-199`,
			};
		}
	}

	return {
		outcome: "not-trusted",
		reason:
			measured.length < trustK
				? `only ${measured.length} block(s) ranged before the scenario ended — fewer than the ` +
					`${trustK} consecutive agreeing blocks the trust gate requires.`
				: "the scenario ended without the trust gate opening.",
		citation: cite(TWIN.trustK),
	};
}
