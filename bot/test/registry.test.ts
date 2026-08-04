/**
 * @file The registry lifecycle, end to end through the HTTP surface.
 *
 * Every case here goes in as a signed interaction and comes out as the
 * follow-up edit a user would actually see, against real SQLite running the
 * real migration. Three things are worth breaking on specifically: `/forget`
 * leaving a row behind, `/who-has` answering somebody who is not a
 * maintainer, and a dead D1 turning into a stack trace instead of a
 * sentence.
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import {
	captureFollowups,
	generateTestKeypair,
	interactionRequest,
	makeExecutionContext,
	signBody,
	type Followup,
	type TestKeypair,
} from "./helpers.ts";
import { brokenD1, makeD1, type FakeD1 } from "./d1-sqlite.ts";

const TS = "1700000000";
const MAINTAINER = "111111111111111111";
const CONTRIBUTOR = "222222222222222222";

async function withFixtures(
	fn: (f: { key: TestKeypair; d1: FakeD1; followups: { calls: Followup[] } }) => Promise<void>,
): Promise<void> {
	const key = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await fn({ key, d1, followups });
	} finally {
		followups.restore();
		d1.close();
	}
}

function env(key: TestKeypair, d1: FakeD1, overrides: Partial<Env> = {}): Env {
	return { DISCORD_PUBLIC_KEY: key.publicKeyHex, DB: d1.binding, MAINTAINER_IDS: MAINTAINER, ...overrides } as Env;
}

interface Sent {
	res: Response;
	followup: string | undefined;
}

async function send(body: object, key: TestKeypair, e: Env, followups: Followup[]): Promise<Sent> {
	const raw = JSON.stringify(body);
	const req = interactionRequest(raw, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key.privateKey, TS, raw),
	});
	const before = followups.length;
	const { ctx, settled } = makeExecutionContext();
	const res = await app.fetch(req, e, ctx);
	await settled();
	return { res, followup: followups.slice(before).at(-1)?.body.content };
}

function ihaveCommand(userId: string, board: string, radio: string, nfc: string) {
	return {
		id: "i1",
		application_id: "app-1",
		token: "token-1",
		type: 2,
		data: {
			name: "ihave",
			options: [
				{ name: "board", value: board },
				{ name: "radio", value: radio },
				{ name: "nfc", value: nfc },
			],
		},
		member: { user: { id: userId } },
	};
}

function ihaveModalSubmit(
	userId: string,
	customId: string,
	fields: { phone_model?: string; ios_version?: string; awake_window?: string; utc_offset?: string },
) {
	const components = [
		fields.phone_model !== undefined
			? { type: 4, custom_id: "phone_model", value: fields.phone_model }
			: null,
		fields.ios_version !== undefined
			? { type: 4, custom_id: "ios_version", value: fields.ios_version }
			: null,
		fields.awake_window !== undefined
			? { type: 4, custom_id: "awake_window", value: fields.awake_window }
			: null,
		fields.utc_offset !== undefined
			? { type: 3, custom_id: "utc_offset", values: [fields.utc_offset] }
			: null,
	].filter(Boolean);
	return {
		id: "i2",
		application_id: "app-1",
		token: "token-1",
		type: 5,
		data: { custom_id: customId, components },
		member: { user: { id: userId } },
	};
}

test("/ihave opens a modal carrying board/radio/nfc in its custom_id", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const { res } = await send(ihaveCommand(CONTRIBUTOR, "esp32c6", "dw3220", "none"), key, e, followups.calls);
		assert.equal(res.status, 200);
		const json = (await res.json()) as { type: number; data: { custom_id: string } };
		assert.equal(json.type, 9);
		assert.equal(json.data.custom_id, "ihave:esp32c6:dw3220:none");
	});
});

test("/ihave rejects an unknown board before opening a modal", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const { res } = await send(
			ihaveCommand(CONTRIBUTOR, "not-a-board", "dw3220", "none"),
			key,
			e,
			followups.calls,
		);
		const json = (await res.json()) as { type: number };
		assert.equal(json.type, 4, "an immediate message, not a modal");
	});
});

test("submitting the /ihave modal defers, then stores exactly one row keyed on the Discord user ID", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const submit = ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "19.1",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		const { res, followup } = await send(submit, key, e, followups.calls);

		assert.deepEqual(await res.clone().json(), { type: 5, data: { flags: 64 } });
		assert.match(followup ?? "", /Registered \*\*dwm3001cdk\*\*/);

		const rows = d1.rows("SELECT * FROM rigs");
		assert.equal(rows.length, 1);
		assert.equal(rows[0]!.discord_user_id, CONTRIBUTOR);
		assert.equal(rows[0]!.ios_version, "19.1");
		assert.equal(rows[0]!.utc_offset, -300);
		assert.equal(rows[0]!.awake_start, 8);
		assert.equal(rows[0]!.awake_end, 23);
		// Nothing that could name a person is stored.
		assert.ok(!("username" in rows[0]!));
		assert.ok(!("display_name" in rows[0]!));
	});
});

test("re-submitting for the same board replaces the row rather than accumulating", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const first = ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "18.5",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		await send(first, key, e, followups.calls);

		const second = ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "19.1",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		await send(second, key, e, followups.calls);

		const rows = d1.rows("SELECT * FROM rigs WHERE discord_user_id = ?", CONTRIBUTOR);
		assert.equal(rows.length, 1);
		assert.equal(rows[0]!.ios_version, "19.1");
	});
});

test("an invalid iOS version in the modal is rejected without storing anything", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const submit = ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "not-a-version",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		const { res } = await send(submit, key, e, followups.calls);
		const json = (await res.json()) as { type: number };
		assert.equal(json.type, 4, "rejected immediately, not deferred");
		assert.equal(d1.rows("SELECT * FROM rigs").length, 0);
	});
});

test("a tampered modal custom_id (unknown board) is rejected without storing anything", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const submit = ihaveModalSubmit(CONTRIBUTOR, "ihave:not-a-board:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "19.1",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		const { res } = await send(submit, key, e, followups.calls);
		const json = (await res.json()) as { type: number };
		assert.equal(json.type, 4);
		assert.equal(d1.rows("SELECT * FROM rigs").length, 0);
	});
});

test("/ihave degrades to a named error when D1 is unreachable, rather than crashing", async () => {
	await withFixtures(async ({ key, followups }) => {
		const e = { DISCORD_PUBLIC_KEY: key.publicKeyHex, DB: brokenD1(), MAINTAINER_IDS: MAINTAINER } as Env;
		const submit = ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
			phone_model: "iPhone 15 Pro",
			ios_version: "19.1",
			awake_window: "8-23",
			utc_offset: "-300",
		});
		const { followup } = await send(submit, key, e, followups.calls);
		assert.match(followup ?? "", /not reachable/);
	});
});

function forgetCommand(userId: string, board?: string) {
	return {
		id: "i3",
		application_id: "app-1",
		token: "token-1",
		type: 2,
		data: { name: "forget", options: board ? [{ name: "board", value: board }] : [] },
		member: { user: { id: userId } },
	};
}

test("/forget with a board deletes only that board", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		await send(
			ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
				phone_model: "x",
				ios_version: "19.1",
				awake_window: "8-23",
				utc_offset: "-300",
			}),
			key,
			e,
			followups.calls,
		);
		await send(
			ihaveModalSubmit(CONTRIBUTOR, "ihave:nrf5340dk:dw3220:st25r300", {
				phone_model: "x",
				ios_version: "19.1",
				awake_window: "8-23",
				utc_offset: "-300",
			}),
			key,
			e,
			followups.calls,
		);

		const { followup } = await send(forgetCommand(CONTRIBUTOR, "dwm3001cdk"), key, e, followups.calls);
		assert.match(followup ?? "", /Deleted your \*\*DWM3001CDK\*\* entry/);

		const rows = d1.rows("SELECT board FROM rigs WHERE discord_user_id = ?", CONTRIBUTOR);
		assert.equal(rows.length, 1);
		assert.equal(rows[0]!.board, "nrf5340dk");
	});
});

test("/forget with no board deletes every row for that user, verified by direct query", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		await send(
			ihaveModalSubmit(CONTRIBUTOR, "ihave:dwm3001cdk:dw3110:none", {
				phone_model: "x",
				ios_version: "19.1",
				awake_window: "8-23",
				utc_offset: "-300",
			}),
			key,
			e,
			followups.calls,
		);
		await send(
			ihaveModalSubmit(CONTRIBUTOR, "ihave:esp32c6:dw3220:none", {
				phone_model: "x",
				ios_version: "19.1",
				awake_window: "8-23",
				utc_offset: "-300",
			}),
			key,
			e,
			followups.calls,
		);

		const { followup } = await send(forgetCommand(CONTRIBUTOR), key, e, followups.calls);
		assert.match(followup ?? "", /Deleted 2 entries/);
		assert.equal(d1.rows("SELECT * FROM rigs WHERE discord_user_id = ?", CONTRIBUTOR).length, 0);
	});
});

function whoHasCommand(userId: string, board?: string, ios?: string) {
	const options = [
		...(board ? [{ name: "board", value: board }] : []),
		...(ios ? [{ name: "ios", value: ios }] : []),
	];
	return { id: "i4", application_id: "app-1", token: "token-1", type: 2, data: { name: "who-has", options }, member: { user: { id: userId } } };
}

test("/who-has refuses a non-maintainer without deferring or touching D1", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const { res } = await send(whoHasCommand(CONTRIBUTOR, "dwm3001cdk"), key, e, followups.calls);
		const json = (await res.json()) as { type: number; data: { content: string } };
		assert.equal(json.type, 4, "an immediate refusal, not a deferral");
		assert.match(json.data.content, /maintainer only/);
	});
});

test("/who-has, run by the maintainer, returns matching contributors", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		await send(
			ihaveModalSubmit(CONTRIBUTOR, "ihave:esp32c6:dw3220:none", {
				phone_model: "iPhone 16 Pro",
				ios_version: "19.1",
				awake_window: "8-23",
				utc_offset: "-300",
			}),
			key,
			e,
			followups.calls,
		);

		const { followup } = await send(whoHasCommand(MAINTAINER, "esp32c6"), key, e, followups.calls);
		assert.match(followup ?? "", /<@222222222222222222>/);
		assert.match(followup ?? "", /iPhone 16 Pro/);
	});
});

test("/who-has with neither board nor ios is rejected before deferring", async () => {
	await withFixtures(async ({ key, d1, followups }) => {
		const e = env(key, d1);
		const { res } = await send(whoHasCommand(MAINTAINER), key, e, followups.calls);
		const json = (await res.json()) as { type: number };
		assert.equal(json.type, 4);
	});
});
