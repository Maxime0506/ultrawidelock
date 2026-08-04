/**
 * @file The drift gate for `/build`'s target list.
 *
 * `src/build-targets.ts` is a hand-copied mirror of
 * `.github/workflows/firmware-builds.yml`'s `workflow_dispatch` choices,
 * because a Worker cannot parse that file at request time. This is what
 * keeps the mirror honest: a target added, renamed or removed in the
 * workflow fails this test until the mirror is updated to match.
 */
import { strict as assert } from "node:assert";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, it } from "node:test";
import { BUILD_TARGETS } from "../src/build-targets.ts";
import { choiceInputOptions } from "../scripts/drift.ts";

const REPO_ROOT = join(import.meta.dirname, "../..");

describe("choiceInputOptions", () => {
	it("reads the items under options:", () => {
		const yaml = [
			"on:",
			"  workflow_dispatch:",
			"    inputs:",
			"      targets:",
			"        type: choice",
			"        default: all",
			"        options:",
			"          - all",
			"          - nrf",
			"          - esp32",
			"jobs:",
		].join("\n");
		assert.deepEqual(choiceInputOptions(yaml), ["all", "nrf", "esp32"]);
	});

	it("skips a comment inside the list", () => {
		const yaml = ["options:", "  - all", "  # a note about nrf", "  - nrf", "jobs:"].join(
			"\n",
		);
		assert.deepEqual(choiceInputOptions(yaml), ["all", "nrf"]);
	});

	it("stops at a line back at or above the options: indentation", () => {
		const yaml = ["options:", "  - all", "jobs:"].join("\n");
		assert.deepEqual(choiceInputOptions(yaml), ["all"]);
	});
});

describe("BUILD_TARGETS", () => {
	it("matches the live workflow_dispatch options exactly, in order", () => {
		const yaml = readFileSync(
			join(REPO_ROOT, ".github/workflows/firmware-builds.yml"),
			"utf8",
		);
		const live = choiceInputOptions(yaml);
		assert.deepEqual(
			BUILD_TARGETS.map((t) => t.value),
			live,
			"src/build-targets.ts has fallen out of sync with firmware-builds.yml's targets input",
		);
	});
});
