/**
 * @file The drift gate for `/size`'s data.
 *
 * src/size-baseline.generated.ts is not hand-written, so its risk is not "a
 * cited line moved" but "the committed extract disagrees with a fresh one" —
 * the same shape of check spec-index.test.ts runs for `/spec`.
 */
import { strict as assert } from "node:assert";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, it } from "node:test";
import { SIZE_BASELINE } from "../src/size-baseline.generated.ts";
import { extractPrimary } from "../scripts/size-baseline-extract.ts";

const REPO_ROOT = join(import.meta.dirname, "../..");

describe("the committed size baseline extract", () => {
	it("matches a fresh extraction from firmware/size-baseline.json", () => {
		const raw = JSON.parse(
			readFileSync(join(REPO_ROOT, "firmware/size-baseline.json"), "utf8"),
		);
		const fresh = extractPrimary(raw);
		assert.deepEqual(
			SIZE_BASELINE,
			fresh,
			"stale: run `npm run size-baseline` in bot/ and commit the result",
		);
	});
});

describe("extractPrimary", () => {
	it("returns null rather than throwing on an unexpected shape", () => {
		for (const bad of [{}, { primary: "x" }, { primary: "x", baselines: {} }, null, "not json"]) {
			assert.equal(extractPrimary(bad), null);
		}
	});

	it("pulls the primary config's board, commit and regions", () => {
		const result = extractPrimary({
			primary: "cfg",
			baselines: {
				cfg: {
					commit: "abc123",
					config: { board: "decawave_dwm3001cdk", extra_conf_file: "x.conf" },
					regions: {
						FLASH: { size: 100, used: 90, free: 10, pct: 90 },
						RAM: { size: 50, used: 40, free: 10, pct: 80 },
					},
				},
			},
		});
		assert.deepEqual(result, {
			config: "cfg",
			commit: "abc123",
			board: "decawave_dwm3001cdk",
			extraConfFile: "x.conf",
			regions: {
				FLASH: { size: 100, used: 90, free: 10, pct: 90 },
				RAM: { size: 50, used: 40, free: 10, pct: 80 },
			},
		});
	});
});
