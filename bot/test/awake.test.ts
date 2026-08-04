import { test } from "node:test";
import assert from "node:assert/strict";
import { isAwakeNow, nextWakeUnixMs } from "../src/awake.ts";

// isAwakeNow only ever uses (Math.floor(nowMs/60000) % 1440), i.e. minutes
// since UTC midnight — the Unix epoch itself starts at a UTC midnight, so
// Date.UTC(y, m, d, H, M) always reduces to exactly H*60+M here regardless
// of the calendar date chosen.
const utc = (h: number, m: number): number => Date.UTC(2026, 7, 4, h, m, 0);

test("UTC+13 (offset 780 min), window 8-23: awake at local 23:00", () => {
	assert.equal(isAwakeNow(780, 8, 23, utc(10, 0)), true); // local = 10:00 + 13h = 23:00
});

test("UTC+13 (offset 780 min), window 8-23: asleep at local 04:00", () => {
	assert.equal(isAwakeNow(780, 8, 23, utc(15, 0)), false); // local = 15:00 + 13h = 04:00 (+1 day)
});

test("UTC-11 (offset -660 min), window 8-23: awake at local 09:00", () => {
	assert.equal(isAwakeNow(-660, 8, 23, utc(20, 0)), true); // local = 20:00 - 11h = 09:00
});

test("UTC-11 (offset -660 min), window 8-23: asleep at local 00:00", () => {
	assert.equal(isAwakeNow(-660, 8, 23, utc(11, 0)), false); // local = 11:00 - 11h = 00:00
});

test("overnight window 22-6 wraps across local midnight: awake at 23:00 and 02:00, asleep at 10:00", () => {
	assert.equal(isAwakeNow(0, 22, 6, utc(23, 0)), true);
	assert.equal(isAwakeNow(0, 22, 6, utc(2, 0)), true);
	assert.equal(isAwakeNow(0, 22, 6, utc(10, 0)), false);
});

test("window bounds are inclusive: exactly the start and end hour both count as awake", () => {
	assert.equal(isAwakeNow(0, 8, 23, utc(8, 0)), true);
	assert.equal(isAwakeNow(0, 8, 23, utc(23, 59)), true);
	assert.equal(isAwakeNow(0, 8, 23, utc(7, 59)), false);
});

test("equal start/end bounds mean always awake (the boards.ts 'always-on infra' convention), any hour or offset", () => {
	assert.equal(isAwakeNow(0, 5, 5, utc(0, 0)), true);
	assert.equal(isAwakeNow(780, 5, 5, utc(15, 0)), true);
	assert.equal(isAwakeNow(-660, 5, 5, utc(23, 0)), true);
});

test("UTC+13 and UTC-11 are exactly 24h apart (the date-line pair) and must agree at the same instant", () => {
	// 780 - (-660) = 1440 minutes = one full day: the two offsets land on
	// the same wall-clock local time for any shared UTC instant, just on
	// calendar dates a day apart. Since isAwakeNow only tracks minutes-of-
	// day, it must return the same answer for both at that instant.
	assert.equal(780 - -660, 1440);
	for (const t of [utc(0, 0), utc(9, 30), utc(15, 45), utc(23, 59)]) {
		assert.equal(isAwakeNow(780, 8, 23, t), isAwakeNow(-660, 8, 23, t));
	}
});

test("nextWakeUnixMs: still before today's start hour returns today's occurrence", () => {
	const now = utc(5, 0); // offset 0, so local == UTC
	const next = nextWakeUnixMs(0, 8, now);
	assert.equal(next, utc(8, 0));
});

test("nextWakeUnixMs: past today's start hour returns tomorrow's occurrence", () => {
	const now = utc(10, 0);
	const next = nextWakeUnixMs(0, 8, now);
	assert.equal(next, utc(8, 0) + 24 * 60 * 60 * 1000);
});

test("nextWakeUnixMs: exactly on the boundary rolls to tomorrow", () => {
	const now = utc(8, 0);
	const next = nextWakeUnixMs(0, 8, now);
	assert.equal(next, utc(8, 0) + 24 * 60 * 60 * 1000);
});

test("nextWakeUnixMs honours a nonzero UTC offset", () => {
	// UTC+13, awake starts at local 8. At UTC 15:00 local is 04:00, so the
	// next local 8am is 4 hours away.
	const now = utc(15, 0);
	const next = nextWakeUnixMs(780, 8, now);
	assert.equal(next, now + 4 * 60 * 60 * 1000);
});
