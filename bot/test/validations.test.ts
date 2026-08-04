import { test } from "node:test";
import assert from "node:assert/strict";
import { latestValidations, recordValidation } from "../src/validations.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";

test("latestValidations returns nothing when the table is empty", async () => {
	const d1 = makeD1();
	try {
		assert.deepEqual(await latestValidations(d1.binding as never), []);
	} finally {
		d1.close();
	}
});

test("latestValidations reports the most recent result per (board, ios_version), not the first", async () => {
	const d1 = makeD1();
	try {
		await recordValidation(d1.binding as never, {
			id: "v1",
			board: "esp32c6",
			iosVersion: "19.1",
			passed: false,
			testedBy: "tester-1",
			requestId: "req-1",
			testedAt: 1_000,
		});
		await recordValidation(d1.binding as never, {
			id: "v2",
			board: "esp32c6",
			iosVersion: "19.1",
			passed: true,
			testedBy: "tester-2",
			requestId: "req-2",
			testedAt: 2_000,
		});

		const results = await latestValidations(d1.binding as never);
		assert.equal(results.length, 1);
		assert.deepEqual(results[0], { board: "esp32c6", iosVersion: "19.1", passed: true });
	} finally {
		d1.close();
	}
});

test("latestValidations keeps board/ios_version pairs independent", async () => {
	const d1 = makeD1();
	try {
		await recordValidation(d1.binding as never, {
			id: "v1",
			board: "esp32c6",
			iosVersion: "19.1",
			passed: true,
			testedBy: "tester-1",
			requestId: null,
			testedAt: 1_000,
		});
		await recordValidation(d1.binding as never, {
			id: "v2",
			board: "dwm3001cdk",
			iosVersion: "19.1",
			passed: false,
			testedBy: "tester-1",
			requestId: null,
			testedAt: 1_000,
		});

		const results = await latestValidations(d1.binding as never);
		assert.equal(results.length, 2);
		assert.deepEqual(
			results.sort((a, b) => a.board.localeCompare(b.board)),
			[
				{ board: "dwm3001cdk", iosVersion: "19.1", passed: false },
				{ board: "esp32c6", iosVersion: "19.1", passed: true },
			],
		);
	} finally {
		d1.close();
	}
});

test("degrades to ValidationsUnavailable rather than throwing a raw D1 error", async () => {
	await assert.rejects(
		() =>
			recordValidation(brokenD1() as never, {
				id: "v1",
				board: "esp32c6",
				iosVersion: "19.1",
				passed: true,
				testedBy: "tester-1",
				requestId: null,
				testedAt: 1_000,
			}),
		{ name: "ValidationsUnavailable" },
	);
});
