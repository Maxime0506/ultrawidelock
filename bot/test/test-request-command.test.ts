import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { captureFollowups, generateTestKeypair, interactionRequest, makeExecutionContext, signBody, type TestKeypair } from "./helpers.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";

const TS = "1700000000";
const MAINTAINER = "111111111111111111";
const NON_MAINTAINER = "999999999999999999";

// Equal start/end is this bot's documented "always awake" convention
// (boards.ts), so this owner is awake at whatever instant the test runs.
const AWAKE_OWNER = "222222222222222222";
// A 2-hour window offset 4-5 hours from the current UTC hour can never
// contain the current hour, so this owner is deterministically asleep
// regardless of when the test suite runs.
const nowHour = new Date().getUTCHours();
const ASLEEP_START = (nowHour + 4) % 24;
const ASLEEP_END = (nowHour + 5) % 24;
const ASLEEP_OWNER = "333333333333333333";

function baseEnv(kp: TestKeypair, db: unknown, overrides: Partial<Env> = {}): Env {
	return {
		DISCORD_PUBLIC_KEY: kp.publicKeyHex,
		DB: db as Env["DB"],
		MAINTAINER_IDS: MAINTAINER,
		DISCORD_BOT_TOKEN: "test-bot-token",
		TEST_QUEUE_CHANNEL_ID: "queue-channel-1",
		...overrides,
	};
}

async function invoke(
	kp: TestKeypair,
	env: Env,
	options: { board?: string; ios?: string; what?: string },
	invoker: string,
) {
	const body = JSON.stringify({
		id: crypto.randomUUID(),
		application_id: "app-1",
		token: "token-1",
		type: 2,
		data: {
			name: "test-request",
			options: [
				options.board !== undefined ? { name: "board", value: options.board } : null,
				options.ios !== undefined ? { name: "ios", value: options.ios } : null,
				options.what !== undefined ? { name: "what", value: options.what } : null,
			].filter(Boolean),
		},
		member: { user: { id: invoker } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(kp.privateKey, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const res = await app.fetch(req, env, ctx);
	await settled();
	return res;
}

test("/test-request refuses a non-maintainer immediately, with no D1 or Discord calls", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env = baseEnv(kp, d1.binding);
		const res = await invoke(kp, env, { board: "esp32c6", what: "check unlock" }, NON_MAINTAINER);
		const json = (await res.json()) as { type: number; data: { content: string } };
		assert.equal(json.type, 4, "immediate response, not deferred");
		assert.match(json.data.content, /maintainer only/);
		assert.equal(followups.calls.length, 0);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-request with an unconfigured bot token or queue channel answers immediately rather than deferring into a guaranteed failure", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env = baseEnv(kp, d1.binding, { DISCORD_BOT_TOKEN: undefined });
		const res = await invoke(kp, env, { board: "esp32c6", what: "check unlock" }, MAINTAINER);
		const json = (await res.json()) as { type: number; data: { content: string } };
		assert.equal(json.type, 4);
		assert.match(json.data.content, /not configured/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-request with nobody registered posts nothing and says so", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env = baseEnv(kp, d1.binding);
		await invoke(kp, env, { board: "esp32c6", what: "check unlock" }, MAINTAINER);
		const channelCalls = followups.calls.filter((c) => c.url.includes("/channels/"));
		assert.equal(channelCalls.length, 0);
		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /Nobody in the registry matches/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-request posts a pending Container, pings only the awake owner, and records both candidates", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		d1.rows(
			`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
			 VALUES (?, 'esp32c6', 'dw3220', 'none', 'x', '19.1', 0, ?, ?, 1)`,
			AWAKE_OWNER,
			nowHour,
			nowHour,
		);
		d1.rows(
			`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
			 VALUES (?, 'esp32c6', 'dw3220', 'none', 'x', '19.1', 0, ?, ?, 1)`,
			ASLEEP_OWNER,
			ASLEEP_START,
			ASLEEP_END,
		);

		const env = baseEnv(kp, d1.binding);
		const res = await invoke(kp, env, { board: "esp32c6", ios: "19.1", what: "check approach unlock" }, MAINTAINER);
		const deferredJson = (await res.json()) as { type: number };
		assert.equal(deferredJson.type, 5, "deferred: this does D1 + 2 external POSTs");

		const posts = followups.calls.filter((c) => c.url.includes("/channels/queue-channel-1/messages") && c.method === "POST");
		assert.equal(posts.length, 2, "one Container post, one awake-only ping");

		const cardCall = posts.find((c) => Array.isArray(c.body.components));
		assert.ok(cardCall, "the Container post carries a components array");
		const container = cardCall!.body.components![0] as { type: number; accent_color: number };
		assert.equal(cardCall!.body.flags, 1 << 15, "IS_COMPONENTS_V2");
		assert.equal(container.type, 17);
		assert.equal(container.accent_color, 0xf1c40f, "pending accent color");

		const pingCall = posts.find((c) => typeof c.body.content === "string");
		assert.ok(pingCall);
		assert.match(pingCall!.body.content ?? "", new RegExp(`<@${AWAKE_OWNER}>`));
		assert.doesNotMatch(pingCall!.body.content ?? "", new RegExp(`<@${ASLEEP_OWNER}>`));
		assert.deepEqual(pingCall!.body.allowed_mentions?.users, [AWAKE_OWNER]);

		const requestRows = d1.rows(`SELECT id, status, message_id, channel_id FROM test_requests`);
		assert.equal(requestRows.length, 1);
		assert.equal(requestRows[0]!.status, "pending");
		assert.equal(requestRows[0]!.channel_id, "queue-channel-1");

		const candidateRows = d1.rows(
			`SELECT discord_user_id, awake_at_request FROM test_request_candidates ORDER BY discord_user_id`,
		);
		assert.equal(candidateRows.length, 2);
		const awakeRow = candidateRows.find((r) => r.discord_user_id === AWAKE_OWNER);
		const asleepRow = candidateRows.find((r) => r.discord_user_id === ASLEEP_OWNER);
		assert.equal(awakeRow?.awake_at_request, 1);
		assert.equal(asleepRow?.awake_at_request, 0);

		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /1 awake now, 1 asleep/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-request degrades to a named error when D1 is unreachable, rather than crashing", async () => {
	const kp = await generateTestKeypair();
	const followups = captureFollowups();
	try {
		const env = baseEnv(kp, brokenD1());
		await invoke(kp, env, { board: "esp32c6", what: "check unlock" }, MAINTAINER);
		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /not reachable/);
	} finally {
		followups.restore();
	}
});
