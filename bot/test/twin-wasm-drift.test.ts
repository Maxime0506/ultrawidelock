/**
 * @file The drift gate for `/twin`'s WASM artifact.
 *
 * src/twin.wasm and src/twin.lock.json are extracted from web-twin/twin.js
 * once (scripts/twin-wasm-extract.ts) and checked in, because `/twin`'s only
 * way to run inside workerd is via a build-time-precompiled `.wasm` module
 * (docs/twin-worker-phase0.md). That makes the checked-in artifact a second
 * copy of firmware behaviour, which is exactly what web-twin/check_constants.py
 * exists to catch drift on elsewhere in this repo. This asserts the same
 * thing here: a rebuilt web-twin/twin.js with no matching re-extraction fails
 * loudly instead of running stale WASM.
 */
import { strict as assert } from "node:assert";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, it } from "node:test";

const REPO_ROOT = join(import.meta.dirname, "../..");
const TWIN_JS = join(REPO_ROOT, "web-twin/twin.js");
const TWIN_WASM = join(import.meta.dirname, "../src/twin.wasm");
const LOCK = join(import.meta.dirname, "../src/twin.lock.json");

function sha256(buf: Buffer): string {
	return createHash("sha256").update(buf).digest("hex");
}

interface Lock {
	source_bytes: number;
	source_sha256: string;
	wasm_bytes: number;
	wasm_sha256: string;
}

describe("twin.wasm drift guard", () => {
	const lock = JSON.parse(readFileSync(LOCK, "utf8")) as Lock;
	const twinJs = readFileSync(TWIN_JS);
	const twinWasm = readFileSync(TWIN_WASM);

	it("web-twin/twin.js byte size still matches the lock", () => {
		assert.equal(
			twinJs.length,
			lock.source_bytes,
			"web-twin/twin.js changed size — run `npm run twin-extract` in bot/ and commit " +
				"src/twin.wasm + src/twin.lock.json",
		);
	});

	it("web-twin/twin.js sha256 still matches the lock", () => {
		assert.equal(
			sha256(twinJs),
			lock.source_sha256,
			"web-twin/twin.js content changed — run `npm run twin-extract` in bot/ and commit " +
				"src/twin.wasm + src/twin.lock.json",
		);
	});

	it("the committed src/twin.wasm still matches the lock", () => {
		assert.equal(twinWasm.length, lock.wasm_bytes);
		assert.equal(sha256(twinWasm), lock.wasm_sha256);
	});

	it("src/twin.wasm is a valid WASM module (magic + version)", () => {
		assert.deepEqual(
			[...twinWasm.subarray(0, 8)],
			[0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00],
		);
	});
});
