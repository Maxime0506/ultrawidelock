import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { captureFollowups, generateTestKeypair, interactionRequest, makeExecutionContext, signBody, type TestKeypair } from "./helpers.ts";
import { makeD1, type FakeD1 } from "./d1-sqlite.ts";
import { claim, createTestRequest } from "../src/testRequests.ts";
import { latestValidations } from "../src/validations.ts";

const TS = "1700000000";
const TESTER = "666666666666666666";
const OTHER_USER = "777777777777777777";

async function invoke(kp: TestKeypair, env: Env, channelId: string | undefined, result: string, invoker: string) {
	const body = JSON.stringify({
		id: crypto.randomUUID(),
		application_id: "app-1",
		token: "token-1",
		type: 2,
		data: { name: "test-result", options: [{ name: "result", value: result }] },
		member: { user: { id: invoker } },
		channel_id: channelId,
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

async function seedClaimed(d1: FakeD1, overrides: Partial<Parameters<typeof createTestRequest>[1]> = {}) {
	await createTestRequest(d1.binding as never, {
		id: "req-1",
		requesterId: "maintainer-1",
		board: "esp32c6",
		iosVersion: "19.1",
		what: "check approach unlock",
		channelId: "queue-1",
		messageId: "card-1",
		createdAt: 1_000,
		candidates: [{ discordUserId: TESTER, awake: true }],
		...overrides,
	});
	await claim(d1.binding as never, "req-1", TESTER);
	// setThread happens via the real Accept flow; here we fake the same
	// effect directly since these tests target /test-result in isolation.
	d1.rows(`UPDATE test_requests SET thread_id = 'thread-1' WHERE id = 'req-1'`);
}

test("/test-result outside any claim thread says so", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		await invoke(kp, env, "some-random-channel", "pass", TESTER);
		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /only works inside a test-request thread/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-result run by someone other than the claimer is refused", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedClaimed(d1);
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		await invoke(kp, env, "thread-1", "pass", OTHER_USER);
		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /Only <@666666666666666666>/);

		const rows = d1.rows(`SELECT status FROM test_requests WHERE id = 'req-1'`);
		assert.equal(rows[0]!.status, "claimed", "unchanged");
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-result pass: closes the request, records a validation, and edits the queue card", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedClaimed(d1);
		const env: Env = {
			DISCORD_PUBLIC_KEY: kp.publicKeyHex,
			DB: d1.binding as Env["DB"],
			DISCORD_BOT_TOKEN: "bot-token",
		};
		await invoke(kp, env, "thread-1", "pass", TESTER);

		const rows = d1.rows(`SELECT status FROM test_requests WHERE id = 'req-1'`);
		assert.equal(rows[0]!.status, "done");

		const results = await latestValidations(d1.binding as never);
		assert.deepEqual(results, [{ board: "esp32c6", iosVersion: "19.1", passed: true }]);

		const editCall = followups.calls.find((c) => c.url.includes("/channels/queue-1/messages/card-1") && c.method === "PATCH");
		assert.ok(editCall, "the original queue card was edited via the bot token");
		const container = editCall!.body.components![0] as { accent_color: number };
		assert.equal(container.accent_color, 0x2ecc71);

		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /Recorded \*\*PASS\*\*/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-result fail: records a failing validation and a red card", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedClaimed(d1);
		const env: Env = {
			DISCORD_PUBLIC_KEY: kp.publicKeyHex,
			DB: d1.binding as Env["DB"],
			DISCORD_BOT_TOKEN: "bot-token",
		};
		await invoke(kp, env, "thread-1", "fail", TESTER);

		const results = await latestValidations(d1.binding as never);
		assert.deepEqual(results, [{ board: "esp32c6", iosVersion: "19.1", passed: false }]);

		const editCall = followups.calls.find((c) => c.url.includes("/channels/queue-1/messages/card-1") && c.method === "PATCH");
		const container = editCall!.body.components![0] as { accent_color: number };
		assert.equal(container.accent_color, 0xe74c3c);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-result submitted twice: the second call reports already-recorded and does not double-write a validation", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedClaimed(d1);
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		await invoke(kp, env, "thread-1", "pass", TESTER);
		await invoke(kp, env, "thread-1", "fail", TESTER);

		const results = await latestValidations(d1.binding as never);
		assert.equal(results.length, 1);
		assert.equal(results[0]!.passed, true, "the first result stands");

		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /already has a result/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/test-result on a request with no iOS version specified closes it but skips the matrix", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seedClaimed(d1, { iosVersion: null });
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		await invoke(kp, env, "thread-1", "pass", TESTER);

		const rows = d1.rows(`SELECT status FROM test_requests WHERE id = 'req-1'`);
		assert.equal(rows[0]!.status, "done");
		assert.deepEqual(await latestValidations(d1.binding as never), []);

		const last = followups.calls.at(-1);
		assert.match(last?.body.content ?? "", /will not appear on `\/matrix`/);
	} finally {
		followups.restore();
		d1.close();
	}
});
