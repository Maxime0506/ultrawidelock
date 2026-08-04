import { test } from "node:test";
import assert from "node:assert/strict";
import { compareVersions, distinctVersions, matrixTable } from "../src/matrix.ts";
import { BOARDS } from "../src/boards.ts";
import type { MatrixCount } from "../src/rigs.ts";

test("compareVersions orders numerically, not lexicographically", () => {
	assert.ok(compareVersions("9.1", "19.1") < 0, "9.1 must sort before 19.1");
	assert.ok(compareVersions("19.1", "19.1.2") < 0, "a shorter prefix sorts before its extension");
	assert.equal(compareVersions("19.1", "19.1"), 0);
	assert.ok(compareVersions("19.10", "19.9") > 0, "numeric, not lexicographic, comparison of segments");
});

test("distinctVersions dedupes and sorts ascending", () => {
	const counts: MatrixCount[] = [
		{ board: "esp32c6", ios_version: "19.1", n: 1 },
		{ board: "dwm3001cdk", ios_version: "9.1", n: 2 },
		{ board: "nrf5340dk", ios_version: "19.1", n: 1 },
	];
	assert.deepEqual(distinctVersions(counts), ["9.1", "19.1"]);
});

test("matrixTable returns null when the registry has no iOS versions at all", () => {
	assert.equal(matrixTable([]), null);
});

test("matrixTable lists every board, including ones with zero owners on every column", () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 3 }];
	const table = matrixTable(counts);
	assert.ok(table);
	for (const b of BOARDS) {
		assert.match(table, new RegExp(escapeRegExp(b.name)), `${b.name} row must be present`);
	}
});

test("a (board, iOS) pair with an owner shows the count and the owned glyph", () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 3 }];
	const table = matrixTable(counts);
	assert.match(table ?? "", /3⚠️/);
});

test("a (board, iOS) pair with no owner shows 0 and the unowned glyph", () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 3 }];
	const table = matrixTable(counts);
	// dwm3001cdk has no rows registered for iOS 19.1 in this fixture.
	assert.match(table ?? "", /0❓/);
});

test("a validation result overrides the owned/unowned glyph with pass/fail", () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 2 }];
	const table = matrixTable(counts, [{ board: "esp32c6", iosVersion: "19.1", passed: true }]);
	assert.match(table ?? "", /2✅/);

	const failed = matrixTable(counts, [{ board: "esp32c6", iosVersion: "19.1", passed: false }]);
	assert.match(failed ?? "", /2❌/);
});

function escapeRegExp(s: string): string {
	return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
