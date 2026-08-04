import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { captureFollowups, generateTestKeypair, interactionRequest, makeExecutionContext, signBody, type TestKeypair } from "./helpers.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";
import { recordValidation } from "../src/validations.ts";

const TS = "1700000000";
const USER = "222222222222222222";
const PNG_MAGIC = [0x89, 0x50, 0x4e, 0x47];

async function invokeMatrix(kp: TestKeypair, env: Env, followupCalls: ReturnType<typeof captureFollowups>["calls"]) {
	const body = JSON.stringify({ id: crypto.randomUUID(), application_id: "app-1", token: "token-1", type: 2, data: { name: "matrix" }, member: { user: { id: USER } } });
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(kp.privateKey, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const before = followupCalls.length;
	const res = await app.fetch(req, env, ctx);
	await settled();
	return { res, followup: followupCalls.slice(before).at(-1) };
}

test("/matrix's text fallback shows a real ✅/❌ from the validations table, not just owned/unowned", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		d1.rows(
			`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
			 VALUES (?, 'esp32c6', 'dw3220', 'none', 'iPhone 16', '19.1', -300, 8, 23, 1)`,
			USER,
		);
		await recordValidation(d1.binding as never, {
			id: "v1",
			board: "esp32c6",
			iosVersion: "19.1",
			passed: false,
			testedBy: "tester-1",
			requestId: null,
			testedAt: 1_000,
		});
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };

		await invokeMatrix(kp, env, followups.calls); // consumes the PNG cooldown
		const { followup } = await invokeMatrix(kp, env, followups.calls); // falls back to text

		assert.equal(followup?.file, undefined, "the cooldown path is text-only");
		assert.match(followup?.body.content ?? "", /ESP32-C6\s+\| 1❌/, "the specific cell shows the validation result, not just the owner count");
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/matrix on an empty registry falls back to text with no file", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		const { res, followup } = await invokeMatrix(kp, env, followups.calls);

		const json = (await res.json()) as { type: number; data?: { flags?: number } };
		assert.equal(json.type, 5, "deferred response");
		assert.equal(json.data?.flags, undefined, "no EPHEMERAL flag on a public command");
		assert.match(followup?.body.content ?? "", /Nobody has registered/);
		assert.equal(followup?.file, undefined);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/matrix with data and a fresh cooldown renders a real PNG attachment", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		d1.rows(
			`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
			 VALUES (?, 'esp32c6', 'dw3220', 'none', 'iPhone 16', '19.1', -300, 8, 23, 1)`,
			USER,
		);
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		const { followup } = await invokeMatrix(kp, env, followups.calls);

		assert.ok(followup?.file, "a PNG should be attached");
		assert.equal(followup!.file!.filename, "matrix.png");
		assert.deepEqual(Array.from(followup!.file!.bytes.slice(0, 4)), PNG_MAGIC, "real PNG magic bytes, not a stub");
		assert.ok(followup!.file!.bytes.length > 1000, "a real rendered image, not an empty file");
		assert.deepEqual(followup?.body.attachments, [{ id: "0", filename: "matrix.png" }]);
		assert.match(followup?.body.content ?? "", /generated <t:\d+:R>/);
		assert.doesNotMatch(followup?.body.content ?? "", /```/, "the PNG path should not also send the text table");
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/matrix's second call within the cooldown window falls back to text, not a second render", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		d1.rows(
			`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
			 VALUES (?, 'esp32c6', 'dw3220', 'none', 'iPhone 16', '19.1', -300, 8, 23, 1)`,
			USER,
		);
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };

		const first = await invokeMatrix(kp, env, followups.calls);
		assert.ok(first.followup?.file, "first call should still render");

		const second = await invokeMatrix(kp, env, followups.calls);
		assert.equal(second.followup?.file, undefined, "second call should not render again");
		assert.match(second.followup?.body.content ?? "", /```/);
		assert.match(second.followup?.body.content ?? "", /cools down/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/matrix notes in the message when columns were truncated, not just silently in the image", async () => {
	const kp = await generateTestKeypair();
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		for (let i = 0; i < 20; i++) {
			d1.rows(
				`INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at)
				 VALUES (?, 'esp32c6', 'dw3220', 'none', 'x', ?, -300, 8, 23, 1)`,
				`user-${i}`,
				`${9 + i}.0`,
			);
		}
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: d1.binding as Env["DB"] };
		const { followup } = await invokeMatrix(kp, env, followups.calls);

		assert.ok(followup?.file, "still renders an image, just a narrower one");
		assert.match(followup?.body.content ?? "", /most recent iOS versions only/);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("/matrix degrades to a named error when D1 is unreachable, rather than crashing", async () => {
	const kp = await generateTestKeypair();
	const followups = captureFollowups();
	try {
		const env: Env = { DISCORD_PUBLIC_KEY: kp.publicKeyHex, DB: brokenD1() as Env["DB"] };
		const { followup } = await invokeMatrix(kp, env, followups.calls);
		assert.match(followup?.body.content ?? "", /not reachable/);
		assert.equal(followup?.file, undefined);
	} finally {
		followups.restore();
	}
});
