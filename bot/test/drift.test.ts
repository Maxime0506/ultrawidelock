/**
 * @file The drift gate, and the triage table it guards.
 *
 * Two halves. The unit cases drive the checker with fixtures, so a failure
 * names which kind of drift it caught. The last two cases run the real table
 * against the real repository, so `npm test` alone catches a stale citation
 * even if somebody skips `npm run drift`.
 */
import { strict as assert } from "node:assert";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, it } from "node:test";
import { allCitations, TOPICS, DEVID } from "../src/citations.ts";
import { checkCitations, uncoveredFiles, workflowPathPatterns } from "../scripts/drift.ts";

const REPO_ROOT = join(import.meta.dirname, "../..");

describe("checkCitations", () => {
	const fixture = (lines: string[]) => (file: string) =>
		file === "mk/cdk.mk" ? lines : null;

	it("passes when the cited line still contains the substring", () => {
		const read = fixture(["one", "two 0xDECA03xx here", "three"]);
		assert.deepEqual(
			checkCitations(read, [{ file: "mk/cdk.mk", line: 2, expect: "0xDECA03xx" }]),
			[],
		);
	});

	it("fails when the cited line has been edited", () => {
		const read = fixture(["one", "two 0xDECA04xx here", "three"]);
		const drifted = checkCitations(read, [
			{ file: "mk/cdk.mk", line: 2, expect: "0xDECA03xx" },
		]);
		assert.equal(drifted.length, 1);
		assert.equal(drifted[0]!.reason, "substring-moved");
		assert.equal(drifted[0]!.actual, "two 0xDECA04xx here");
	});

	it("fails when the substring moved to a different line", () => {
		// The exact scenario the gate exists for: an insert above the citation.
		const read = fixture(["inserted", "one", "two 0xDECA03xx here", "three"]);
		const drifted = checkCitations(read, [
			{ file: "mk/cdk.mk", line: 2, expect: "0xDECA03xx" },
		]);
		assert.equal(drifted.length, 1);
		assert.equal(drifted[0]!.reason, "substring-moved");
	});

	it("fails when the file lost the line entirely", () => {
		const drifted = checkCitations(fixture(["one"]), [
			{ file: "mk/cdk.mk", line: 40, expect: "anything" },
		]);
		assert.equal(drifted[0]!.reason, "missing-line");
	});

	it("fails when the file is gone", () => {
		const drifted = checkCitations(fixture(["one"]), [
			{ file: "mk/gone.mk", line: 1, expect: "anything" },
		]);
		assert.equal(drifted[0]!.reason, "missing-file");
	});
});

describe("workflow path coverage", () => {
	const yaml = [
		"on:",
		"  push:",
		"    paths:",
		"      - bot/**",
		"      - mk/cdk.mk",
		"  pull_request:",
		"    paths:",
		"      - docs/home-assistant.md",
		"  workflow_dispatch:",
		"jobs:",
	].join("\n");

	it("reads patterns from every trigger block", () => {
		assert.deepEqual(workflowPathPatterns(yaml), [
			"bot/**",
			"mk/cdk.mk",
			"docs/home-assistant.md",
		]);
	});

	it("stops reading at the end of a list", () => {
		// `workflow_dispatch:` and `jobs:` must not be mistaken for paths.
		assert.ok(!workflowPathPatterns(yaml).includes("workflow_dispatch:"));
	});

	it("does not treat a comment inside the list as the end of it", () => {
		const withComment = [
			"on:",
			"  push:",
			"    paths:",
			"      - bot/**",
			"      # explains the next line",
			"      - docs/**",
			"jobs:",
		].join("\n");
		assert.deepEqual(workflowPathPatterns(withComment), ["bot/**", "docs/**"]);
	});

	it("counts an exact path and a ** prefix as covered", () => {
		const patterns = workflowPathPatterns(yaml);
		assert.deepEqual(uncoveredFiles(patterns, ["mk/cdk.mk", "bot/src/citations.ts"]), []);
	});

	it("reports a cited file no trigger covers", () => {
		const patterns = workflowPathPatterns(yaml);
		assert.deepEqual(uncoveredFiles(patterns, ["firmware/prj.conf"]), ["firmware/prj.conf"]);
	});
});

describe("the real triage table", () => {
	const read = (file: string): string[] | null => {
		try {
			return readFileSync(join(REPO_ROOT, file), "utf8").split("\n");
		} catch {
			return null;
		}
	};

	it("cites lines that still say what it claims", () => {
		const drifted = checkCitations(read, allCitations());
		assert.deepEqual(
			drifted.map((d) => `${d.file}:${d.line} (${d.reason})`),
			[],
		);
	});

	it("cites only files that trigger the bot workflow", () => {
		const yaml = readFileSync(join(REPO_ROOT, ".github/workflows/bot.yml"), "utf8");
		const uncovered = uncoveredFiles(
			workflowPathPatterns(yaml),
			allCitations().map((c) => c.file),
		);
		assert.deepEqual(uncovered, []);
	});

	it("gives every topic at least one citation", () => {
		for (const t of TOPICS) {
			assert.ok(t.citations.length > 0, `${t.id} has no citation`);
		}
		assert.ok(DEVID.citations.length > 0);
	});

	it("has no duplicate topic ids", () => {
		const ids = TOPICS.map((t) => t.id);
		assert.equal(new Set(ids).size, ids.length);
	});
});
