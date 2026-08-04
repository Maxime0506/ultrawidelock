import { test } from "node:test";
import assert from "node:assert/strict";
import { checkMatrixCooldown } from "../src/cooldown.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";
import type { D1Database } from "@cloudflare/workers-types";

test("first call is always ready", async () => {
	const d1 = makeD1();
	try {
		const res = await checkMatrixCooldown(d1.binding as D1Database, "user-1", 30_000, 1_000_000);
		assert.deepEqual(res, { ready: true });
	} finally {
		d1.close();
	}
});

test("a second call inside the window is not ready, and reports remaining time", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await checkMatrixCooldown(db, "user-1", 30_000, 1_000_000);
		const res = await checkMatrixCooldown(db, "user-1", 30_000, 1_010_000);
		assert.equal(res.ready, false);
		if (!res.ready) assert.equal(res.remainingMs, 20_000);
	} finally {
		d1.close();
	}
});

test("a call after the window has elapsed is ready again, and restarts the window", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await checkMatrixCooldown(db, "user-1", 30_000, 1_000_000);
		const res = await checkMatrixCooldown(db, "user-1", 30_000, 1_030_000);
		assert.deepEqual(res, { ready: true });
	} finally {
		d1.close();
	}
});

test("different users have independent cooldowns", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await checkMatrixCooldown(db, "user-1", 30_000, 1_000_000);
		const res = await checkMatrixCooldown(db, "user-2", 30_000, 1_000_001);
		assert.deepEqual(res, { ready: true });
	} finally {
		d1.close();
	}
});

test("a missing D1 binding fails open (ready), rather than blocking the fallback path", async () => {
	const res = await checkMatrixCooldown(undefined, "user-1", 30_000, 1_000_000);
	assert.deepEqual(res, { ready: true });
});

test("a D1 failure fails open (ready), rather than throwing", async () => {
	const res = await checkMatrixCooldown(brokenD1() as D1Database, "user-1", 30_000, 1_000_000);
	assert.deepEqual(res, { ready: true });
});
