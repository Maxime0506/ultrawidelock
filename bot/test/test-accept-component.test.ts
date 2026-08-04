import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { captureFollowups, FAKE_THREAD_ID, generateTestKeypair, interactionRequest, makeExecutionContext, signBody, type TestKeypair } from "./helpers.ts";
import { makeD1 } from "./d1-sqlite.ts";
import { createTestRequest } from "../src/testRequests.ts";
import { acceptCustomId } from "../src/testRequestContainer.ts";

const TS = "1700000000";
const ACCEPTER_A = "444444444444444444";
const ACCEPTER_B = "555555555555555555";

async function invokeAccept(kp: TestKeypair, env: Env, customId: string, invoker: string) {
	const body = JSON.stringify({
		id: crypto.randomUUID(),
		application_id: "app-1",
		token: "token-1",
		type: 3, // MESSAGE_COMPONENT
		data: { custom_id: customId, component_type: 2 },
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

async function seedRequest(db: unknown, id: string) {
	await createTestRequest(db as never, {
		id,
		requesterId: "maintainer-1",
		board: "esp32c6",
		iosVersion: "19.1",
		what: "check approach unlock",
		channelId: "queue-channel-1",
		messageId: "card-msg-1",
		createdAt: 1_000,
		candidates: [{ discordUserId: ACCEPTER_A, awake: true }],
	});
}

test("Accept: a custom_id nothing routes to gets index.ts's generic 'not handled' message", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		const res = await invokeAccept(kp, env, "garbage", ACCEPTER_A);
		const json = (await res.json()) as { type: number; data: { flags?: number; content: string } };
		assert.equal(json.type, 4);
		assert.equal(json.data.flags, 64);
		assert.match(json.data.content, /not one this Worker handles/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("Accept: a well-prefixed but malformed custom_id gets componentHandler's own 'tampered with' message", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		const res = await invokeAccept(kp, env, "test-accept:", ACCEPTER_A);
		const json = (await res.json()) as { type: number; data: { flags?: number; content: string } };
		assert.equal(json.type, 4);
		assert.equal(json.data.flags, 64);
		assert.match(json.data.content, /expired or was tampered with/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("Accept: first click claims, starts a thread, and edits the card in place to CLAIMED", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedRequest(d1.binding, "req-x");
		const env: Env = {
			DISCORD_PUBLIC_KEY: kp.publicKeyHex,
			DB: d1.binding as Env["DB"],
			DISCORD_BOT_TOKEN: "bot-token",
		};

		const res = await invokeAccept(kp, env, acceptCustomId("req-x"), ACCEPTER_A);
		const json = (await res.json()) as { type: number };
		assert.equal(json.type, 6, "DEFERRED_UPDATE_MESSAGE");

		const threadCalls = followups.calls.filter((c) => c.url.includes("/threads") && c.method === "POST");
		assert.equal(threadCalls.length, 1);
		assert.ok(threadCalls[0]!.url.includes("/channels/queue-channel-1/messages/card-msg-1/threads"));

		const editCall = followups.calls.find((c) => c.url.includes("/webhooks/") && c.method === "PATCH");
		assert.ok(editCall, "the card was edited via the interaction's own token");
		const container = editCall!.body.components![0] as { type: number; accent_color: number };
		assert.equal(editCall!.body.flags, 1 << 15);
		assert.equal(container.type, 17);
		assert.equal(container.accent_color, 0x5865f2, "claimed accent color");

		const rows = d1.rows(`SELECT status, claimed_by, thread_id FROM test_requests WHERE id = 'req-x'`);
		assert.equal(rows[0]!.status, "claimed");
		assert.equal(rows[0]!.claimed_by, ACCEPTER_A);
		assert.equal(
			rows[0]!.thread_id,
			FAKE_THREAD_ID,
			"the fake thread id captureFollowups' stub hands back for a /threads POST",
		);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("Accept: the losing click gets a private 'already accepted' note and never touches the card", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedRequest(d1.binding, "req-y");
		const env: Env = {
			DISCORD_PUBLIC_KEY: kp.publicKeyHex,
			DB: d1.binding as Env["DB"],
			DISCORD_BOT_TOKEN: "bot-token",
		};

		await invokeAccept(kp, env, acceptCustomId("req-y"), ACCEPTER_A);
		followups.calls.length = 0; // only care about the second click from here

		await invokeAccept(kp, env, acceptCustomId("req-y"), ACCEPTER_B);

		const patchCalls = followups.calls.filter((c) => c.method === "PATCH");
		assert.equal(patchCalls.length, 0, "the loser's click must not edit the card");

		const postCalls = followups.calls.filter((c) => c.url.includes("/webhooks/") && c.method === "POST");
		assert.equal(postCalls.length, 1, "a new ephemeral follow-up, not a card edit");
		assert.match(postCalls[0]!.body.content ?? "", /already accepted/);
		assert.equal(postCalls[0]!.body.flags, 64);

		const rows = d1.rows(`SELECT claimed_by FROM test_requests WHERE id = 'req-y'`);
		assert.equal(rows[0]!.claimed_by, ACCEPTER_A, "still the first accepter");
	} finally {
		followups.restore();
		d1.close();
	}
});
