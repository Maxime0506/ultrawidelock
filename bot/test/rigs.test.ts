import { test } from "node:test";
import assert from "node:assert/strict";
import {
	RegistryUnavailable,
	countForUser,
	entriesForUser,
	forgetRig,
	upsertRig,
	whoHas,
} from "../src/rigs.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";
import type { D1Database } from "@cloudflare/workers-types";

function row(overrides: Partial<Parameters<typeof upsertRig>[1]> = {}) {
	return {
		discord_user_id: "user-1",
		board: "dwm3001cdk",
		radio: "dw3110",
		nfc: "none",
		phone_model: "iPhone 15 Pro",
		ios_version: "19.1",
		utc_offset: -300,
		awake_start: 8,
		awake_end: 23,
		...overrides,
	};
}

test("a user with two boards is two rows (composite key)", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ board: "dwm3001cdk" }));
		await upsertRig(db, row({ board: "nrf5340dk", nfc: "st25r300" }));

		const entries = await entriesForUser(db, "user-1");
		assert.equal(entries.length, 2);
		assert.deepEqual(
			entries.map((e) => e.board).sort(),
			["dwm3001cdk", "nrf5340dk"],
		);
		assert.equal(await countForUser(db, "user-1"), 2);
	} finally {
		d1.close();
	}
});

test("re-running /ihave for the same board replaces the row, not accumulates", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ ios_version: "18.5" }));
		await upsertRig(db, row({ ios_version: "19.1" }));

		const entries = await entriesForUser(db, "user-1");
		assert.equal(entries.length, 1);
		assert.equal(entries[0]?.ios_version, "19.1");
	} finally {
		d1.close();
	}
});

test("/forget with a board deletes only that board", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ board: "dwm3001cdk" }));
		await upsertRig(db, row({ board: "nrf5340dk" }));

		const removed = await forgetRig(db, "user-1", "dwm3001cdk");
		assert.equal(removed, 1);

		const entries = await entriesForUser(db, "user-1");
		assert.equal(entries.length, 1);
		assert.equal(entries[0]?.board, "nrf5340dk");
	} finally {
		d1.close();
	}
});

test("/forget with no board deletes every row for that user, and no other user's", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ discord_user_id: "user-1", board: "dwm3001cdk" }));
		await upsertRig(db, row({ discord_user_id: "user-1", board: "nrf5340dk" }));
		await upsertRig(db, row({ discord_user_id: "user-2", board: "dwm3001cdk" }));

		const removed = await forgetRig(db, "user-1");
		assert.equal(removed, 2);
		assert.equal(await countForUser(db, "user-1"), 0);
		assert.equal(await countForUser(db, "user-2"), 1);
	} finally {
		d1.close();
	}
});

test("/forget on an empty registry deletes nothing and does not throw", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		assert.equal(await forgetRig(db, "nobody"), 0);
	} finally {
		d1.close();
	}
});

test("whoHas filters by board", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ discord_user_id: "user-1", board: "dwm3001cdk" }));
		await upsertRig(db, row({ discord_user_id: "user-2", board: "esp32c6" }));

		const results = await whoHas(db, { board: "dwm3001cdk" });
		assert.equal(results.length, 1);
		assert.equal(results[0]?.discord_user_id, "user-1");
	} finally {
		d1.close();
	}
});

test("whoHas filters by board and iOS version together", async () => {
	const d1 = makeD1();
	try {
		const db = d1.binding as D1Database;
		await upsertRig(db, row({ discord_user_id: "user-1", board: "esp32c6", ios_version: "19.1" }));
		await upsertRig(db, row({ discord_user_id: "user-2", board: "esp32c6", ios_version: "18.5" }));

		const results = await whoHas(db, { board: "esp32c6", iosVersion: "19.1" });
		assert.equal(results.length, 1);
		assert.equal(results[0]?.discord_user_id, "user-1");
	} finally {
		d1.close();
	}
});

test("a missing D1 binding raises RegistryUnavailable rather than throwing raw", async () => {
	await assert.rejects(() => upsertRig(undefined, row()), RegistryUnavailable);
	await assert.rejects(() => forgetRig(undefined, "user-1"), RegistryUnavailable);
	await assert.rejects(() => whoHas(undefined, { board: "dwm3001cdk" }), RegistryUnavailable);
});

test("a D1 failure is wrapped as RegistryUnavailable, cause preserved", async () => {
	const broken = brokenD1() as D1Database;
	await assert.rejects(() => upsertRig(broken, row()), (err: unknown) => {
		assert.ok(err instanceof RegistryUnavailable);
		assert.ok(err.cause instanceof Error);
		return true;
	});
});
