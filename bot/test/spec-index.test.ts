/**
 * @file The drift gate for `/spec`.
 *
 * Unlike src/citations.ts, this index is not hand-written: it is a scan of
 * docs/*.md, committed so the Worker can bundle it without reading the
 * filesystem at request time. The risk that replaces is "the committed file
 * disagrees with a fresh scan", so that is what this asserts, against the
 * live tree and also against fixtures that exercise the two citation shapes
 * and the false positives they could produce.
 */
import { strict as assert } from "node:assert";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, it } from "node:test";
import { SPEC_CITATIONS } from "../src/spec-index.generated.ts";
import { scanDocs, scanText } from "../scripts/spec-scan.ts";

const REPO_ROOT = join(import.meta.dirname, "../..");

describe("the committed spec index", () => {
	it("matches a fresh scan of docs/", () => {
		const fresh = scanDocs(join(REPO_ROOT, "docs"), "docs/");
		assert.deepEqual(
			SPEC_CITATIONS,
			fresh,
			"stale: run `npm run spec-index` in bot/ and commit the result",
		);
	});

	it("cites lines that still contain the token literally", () => {
		// A weaker, context-free check than "matches a fresh scan" above: even
		// without reconstructing table state, the raw text of the token must
		// still be sitting on the line the index points at.
		for (const c of SPEC_CITATIONS) {
			const lines = readFileSync(join(REPO_ROOT, c.file), "utf8").split("\n");
			const line = lines[c.line - 1];
			assert.ok(line !== undefined, `${c.file}:${c.line} does not exist`);
			assert.ok(line.includes(c.section), `${c.file}:${c.line} no longer contains "${c.section}"`);
		}
	});

	it("is not empty", () => {
		assert.ok(SPEC_CITATIONS.length > 0);
	});
});

describe("scanText", () => {
	it("reads the inline form", () => {
		const found = scanText("f.md", "See Aliro 1.0 §14 for the worked example.");
		assert.deepEqual(found, [{ section: "14", file: "f.md", line: 1 }]);
	});

	it("reads the bare-parenthetical form, but not a two-part version number", () => {
		assert.deepEqual(scanText("f.md", "(Aliro 1.0, 11.7.3.4.1) is the gate."), [
			{ section: "11.7.3.4.1", file: "f.md", line: 1 },
		]);
		// "Aliro 1.0" on its own must not self-match as citing section "1.0".
		assert.deepEqual(scanText("f.md", "released as Aliro 1.0 in February."), []);
	});

	it("reads every row of a table whose header names Aliro 1.0", () => {
		const table = [
			"| Fact | Aliro 1.0 |",
			"| --- | --- |",
			"| INS = 0x80 | Table 8-3 |",
			"| INS = 0x81 | Table 8-9, §8.3.3.5.1 |",
			"",
			"Prose after the table.",
		].join("\n");
		const found = scanText("f.md", table);
		assert.deepEqual(found, [
			{ section: "Table 8-3", file: "f.md", line: 3 },
			{ section: "Table 8-9", file: "f.md", line: 4 },
			{ section: "8.3.3.5.1", file: "f.md", line: 4 },
		]);
	});

	it("does not treat a table with no Aliro 1.0 column as a spec table", () => {
		const table = ["| Fact | Value |", "| --- | --- |", "| x | §14 in some other doc |"].join(
			"\n",
		);
		// §14 still matches as inline text (line contains no "Aliro 1.0", so this
		// specific row would only be caught if it separately said "Aliro 1.0").
		// The point of this test is the header alone must not open table mode.
		assert.deepEqual(scanText("f.md", table), []);
	});

	it("closes table mode at the first non-table line", () => {
		const table = [
			"| Fact | Aliro 1.0 |",
			"| --- | --- |",
			"| a | §1 |",
			"not a table row",
			"| b | §2 |",
		].join("\n");
		const found = scanText("f.md", table);
		assert.deepEqual(found, [{ section: "1", file: "f.md", line: 3 }]);
	});

	it("does not fire on our own internal section markers", () => {
		// protocol-research.md cross-references itself with bare §N. Those are
		// not Aliro 1.0 citations and must not appear in the index.
		assert.deepEqual(scanText("f.md", "See §4 above for the transaction."), []);
	});
});
