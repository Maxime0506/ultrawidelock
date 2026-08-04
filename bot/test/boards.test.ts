import { test } from "node:test";
import assert from "node:assert/strict";
import {
	UTC_OFFSETS,
	isKnownBoard,
	isKnownNfc,
	isKnownRadio,
	isKnownUtcOffsetMinutes,
	isValidIosVersion,
	parseAwakeWindow,
} from "../src/boards.ts";

test("UTC offset select is exactly 25 options (Discord's select cap) spanning UTC-11..UTC+13", () => {
	assert.equal(UTC_OFFSETS.length, 25);
	assert.equal(UTC_OFFSETS[0]?.minutes, -11 * 60);
	assert.equal(UTC_OFFSETS.at(-1)?.minutes, 13 * 60);
	assert.ok(isKnownUtcOffsetMinutes(-11 * 60), "UTC-11 (date line west edge) must be selectable");
	assert.ok(isKnownUtcOffsetMinutes(13 * 60), "UTC+13 (date line east edge) must be selectable");
	assert.equal(isKnownUtcOffsetMinutes(14 * 60), false);
	assert.equal(isKnownUtcOffsetMinutes(-12 * 60), false);
});

test("board/radio/nfc enums reject unknown values", () => {
	assert.ok(isKnownBoard("dwm3001cdk"));
	assert.equal(isKnownBoard("not-a-board"), false);
	assert.ok(isKnownRadio("dw3220"));
	assert.equal(isKnownRadio("dw9999"), false);
	assert.ok(isKnownNfc("none"));
	assert.equal(isKnownNfc("nfc-9000"), false);
});

test("iOS version pattern accepts the documented examples", () => {
	assert.ok(isValidIosVersion("19.1"));
	assert.ok(isValidIosVersion("19.1.2"));
	assert.ok(isValidIosVersion("19"));
});

test("iOS version pattern rejects garbage", () => {
	for (const bad of ["", "v19.1", "19.1.2.3", "19..1", "19.1 ", "abc", "19.1a"]) {
		assert.equal(isValidIosVersion(bad), false, `expected "${bad}" to be rejected`);
	}
});

test("awake window parses valid ranges including both boundary hours", () => {
	assert.deepEqual(parseAwakeWindow("8-23"), { start: 8, end: 23 });
	assert.deepEqual(parseAwakeWindow("0-23"), { start: 0, end: 23 });
	assert.deepEqual(parseAwakeWindow("0-0"), { start: 0, end: 0 });
	assert.deepEqual(parseAwakeWindow(" 8-23 "), { start: 8, end: 23 });
});

test("awake window accepts a window that wraps past midnight (start > end)", () => {
	// "22-6" means awake 22:00 through 06:00 local, not an error: the parser
	// only validates shape, not ordering. Awake-computation math (phase 5)
	// is what has to handle the wrap, not this parser.
	assert.deepEqual(parseAwakeWindow("22-6"), { start: 22, end: 6 });
});

test("awake window rejects out-of-range or malformed input", () => {
	for (const bad of ["24-8", "8-24", "-1-8", "8", "8-8-8", "a-b", ""]) {
		assert.equal(parseAwakeWindow(bad), null, `expected "${bad}" to be rejected`);
	}
});
