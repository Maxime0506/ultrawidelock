/**
 * @file Extract the WASM module embedded in web-twin/twin.js into src/twin.wasm.
 *
 * `/twin` runs the compiled firmware inside a Cloudflare Worker, not a
 * browser. workerd refuses runtime WASM code generation from a byte array
 * (`WebAssembly.instantiate(bytes, …)` — the exact path twin.js's own
 * `findWasmBinary()`/`instantiateArrayBuffer()` take — fails there with
 * "Wasm code generation disallowed by embedder"; confirmed directly against
 * `wrangler dev`, see docs/twin-worker-phase0.md). A WASM module imported as
 * a build-time module resource (`import wasmModule from "./twin.wasm"`) is
 * compiled ahead of time instead, which workerd does allow, and Emscripten's
 * `Module["instantiateWasm"]` hook lets a caller supply that precompiled
 * module without twin.js ever reaching its own embedded-bytes path.
 *
 * This script produces that file — once, checked in — by requiring the real,
 * unmodified twin.js under Node (where runtime WASM codegen is allowed) and
 * capturing the exact bytes its own `WebAssembly.instantiate` call receives.
 * twin.js is never edited, forked, or re-encoded; only observed.
 *
 * Run after any web-twin/twin.js rebuild: `npm run twin-extract` in bot/,
 * then commit src/twin.wasm and src/twin.lock.json together. `npm test`'s
 * drift guard (test/twin-wasm-drift.test.ts) fails if twin.js changes without
 * a matching re-extraction.
 */
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const REPO_ROOT = join(import.meta.dirname, "../..");
const TWIN_JS = join(REPO_ROOT, "web-twin/twin.js");
const OUT_WASM = join(import.meta.dirname, "../src/twin.wasm");
const OUT_LOCK = join(import.meta.dirname, "../src/twin.lock.json");

function sha256(buf: Buffer | Uint8Array): string {
	return createHash("sha256").update(buf).digest("hex");
}

async function main() {
	const twinJsSource = readFileSync(TWIN_JS, "utf8");

	let captured: Uint8Array | null = null;
	const originalInstantiate = WebAssembly.instantiate;
	WebAssembly.instantiate = ((bytesOrModule: unknown, imports?: WebAssembly.Imports) => {
		if (bytesOrModule instanceof Uint8Array) captured = bytesOrModule;
		else if (bytesOrModule instanceof ArrayBuffer) captured = new Uint8Array(bytesOrModule);
		return originalInstantiate.call(WebAssembly, bytesOrModule as never, imports);
	}) as typeof WebAssembly.instantiate;

	try {
		// twin.js is a UMD module (`module.exports = createTwin` under CJS); Node's
		// type-stripping runtime (this script runs under `node --experimental-strip-types`
		// via package.json's script, same as every other bot/scripts/*.ts) still
		// resolves a bare require() for a plain .js CommonJS file.
		const { createRequire } = await import("node:module");
		const require = createRequire(import.meta.url);
		const createTwin = require(TWIN_JS) as (opts: unknown) => Promise<{ _twin_boot(): number }>;
		const m = await createTwin({ print: () => {} });
		if (m._twin_boot() !== 0) throw new Error("twin_boot() returned nonzero during extraction");
	} finally {
		WebAssembly.instantiate = originalInstantiate;
	}

	if (!captured) throw new Error("no WebAssembly.instantiate call observed — twin.js's loader shape changed");
	const wasmBytes: Uint8Array = captured;

	writeFileSync(OUT_WASM, wasmBytes);

	const lock = {
		source: "web-twin/twin.js",
		source_bytes: Buffer.byteLength(twinJsSource, "utf8"),
		source_sha256: sha256(Buffer.from(twinJsSource, "utf8")),
		wasm_bytes: wasmBytes.length,
		wasm_sha256: sha256(wasmBytes),
		extracted_at: new Date().toISOString(),
	};
	writeFileSync(OUT_LOCK, JSON.stringify(lock, null, 2) + "\n");

	console.log(
		`wrote src/twin.wasm (${lock.wasm_bytes} bytes, sha256 ${lock.wasm_sha256.slice(0, 16)}…) ` +
			`from web-twin/twin.js (${lock.source_bytes} bytes, sha256 ${lock.source_sha256.slice(0, 16)}…)`,
	);
}

main().catch((err) => {
	console.error(err);
	process.exitCode = 1;
});
