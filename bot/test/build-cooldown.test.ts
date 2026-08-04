/**
 * @file The cooldown's atomicity, against real SQLite.
 *
 * The claim in db.ts is that `checkAndStartCooldown` is race-free because the
 * check and the write are one guarded UPSERT rather than a select followed by
 * a write. This drives it directly, including the case the API-level /build
 * tests cannot reach: two calls at the exact same millisecond.
 */
import { strict as assert } from "node:assert";
import { afterEach, beforeEach, describe, it } from "node:test";
import { checkAndStartCooldown } from "../src/db.ts";
import { brokenD1, makeD1, type FakeD1 } from "./d1-sqlite.ts";

const USER = "222222222222222222";
const COOLDOWN = 900_000;

let d1: FakeD1;
beforeEach(() => (d1 = makeD1()));
afterEach(() => d1.close());

describe("checkAndStartCooldown", () => {
	it("admits a first-time user", async () => {
		const result = await checkAndStartCooldown(d1.binding as never, USER, COOLDOWN, 1_000_000);
		assert.deepEqual(result, { ready: true });
	});

	it("blocks a second call inside the window and reports the remainder", async () => {
		const now = 1_000_000;
		await checkAndStartCooldown(d1.binding as never, USER, COOLDOWN, now);
		const second = await checkAndStartCooldown(
			d1.binding as never,
			USER,
			COOLDOWN,
			now + 100_000,
		);
		assert.equal(second.ready, false);
		if (!second.ready) assert.equal(second.remainingMs, COOLDOWN - 100_000);
	});

	it("admits a third call once the window has passed", async () => {
		const now = 1_000_000;
		await checkAndStartCooldown(d1.binding as never, USER, COOLDOWN, now);
		const later = await checkAndStartCooldown(
			d1.binding as never,
			USER,
			COOLDOWN,
			now + COOLDOWN + 1,
		);
		assert.deepEqual(later, { ready: true });
	});

	it("only ever admits one call at the identical millisecond", async () => {
		// The scenario a select-then-write implementation gets wrong: both
		// calls would read "no row yet" before either writes. The guarded
		// UPSERT here is one statement, so only the first still gets through.
		const now = 1_000_000;
		const [a, b] = await Promise.all([
			checkAndStartCooldown(d1.binding as never, USER, COOLDOWN, now),
			checkAndStartCooldown(d1.binding as never, USER, COOLDOWN, now),
		]);
		const readyCount = [a, b].filter((r) => r.ready).length;
		assert.equal(readyCount, 1, "exactly one of the two simultaneous calls must be admitted");
	});

	it("tracks users independently", async () => {
		const now = 1_000_000;
		await checkAndStartCooldown(d1.binding as never, "user-a", COOLDOWN, now);
		const other = await checkAndStartCooldown(d1.binding as never, "user-b", COOLDOWN, now);
		assert.deepEqual(other, { ready: true });
	});

	it("throws RegistryUnavailable rather than a driver error when D1 is down", async () => {
		await assert.rejects(
			() => checkAndStartCooldown(brokenD1() as never, USER, COOLDOWN, 1_000_000),
			(err: unknown) => err instanceof Error && err.name === "RegistryUnavailable",
		);
	});
});
