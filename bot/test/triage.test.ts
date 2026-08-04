/**
 * @file `/why` and `/decode-devid`.
 *
 * The case that matters most is the one where the bot knows nothing:
 * an unrecognised DEV_ID must produce "no known signature" and must not
 * produce a reading. Everything else here is checking that an answer never
 * leaves without its citation.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { normalise } from "../src/commands/decode-devid.ts";
import { cite, DEVID, TOPICS } from "../src/citations.ts";
import {
	interactionRequest,
	makeExecutionContext,
	makeKey,
	signBody,
	type TestKey,
} from "./helpers.ts";

const TS = "1700000000";

interface Reply {
	type: number;
	data: { content: string; flags?: number; allowed_mentions: { parse: string[] } };
}

async function invoke(
	key: TestKey,
	name: string,
	options: { name: string; value: string }[],
): Promise<Reply> {
	const body = JSON.stringify({
		id: "i1",
		application_id: "app1",
		token: "tok1",
		type: 2,
		data: { name, options },
		member: { user: { id: "222222222222222222" } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const res = await worker.fetch(req, { DISCORD_PUBLIC_KEY: key.publicKeyHex } as Env, ctx);
	await settled();
	return (await res.json()) as Reply;
}

describe("normalise", () => {
	it("accepts the forms a log or a person produces", () => {
		assert.equal(normalise("0xDECA0302"), "deca0302");
		assert.equal(normalise("DECA0302"), "deca0302");
		assert.equal(normalise("  deca0302  "), "deca0302");
		assert.equal(normalise("0X0"), "00000000");
	});

	it("rejects anything that is not hex", () => {
		for (const bad of ["", "hello", "0xZZ", "0x1234567890", "12 34"]) {
			assert.equal(normalise(bad), null, bad);
		}
	});
});

describe("/decode-devid", () => {
	it("reads a healthy DEV_ID and cites it", async () => {
		const key = await makeKey();
		const reply = await invoke(key, "decode-devid", [{ name: "value", value: "0xDECA0302" }]);
		assert.match(reply.data.content, /is answering/);
		// Derived from the table rather than transcribed. A literal here is a
		// fourth place a line number lives, and the drift gate does not read
		// this file: when main moved mk/cdk.mk these assertions failed for the
		// right reason by accident, which is not a property to rely on twice.
		assert.ok(reply.data.content.includes(cite(DEVID.citations[0]!)), "cites the healthy read");
		assert.equal(reply.data.flags, undefined, "triage answers are public");
	});

	it("reads both documented dead values", async () => {
		const key = await makeKey();
		for (const value of ["0x00000000", "0xFFFFFFFF"]) {
			const reply = await invoke(key, "decode-devid", [{ name: "value", value }]);
			assert.match(reply.data.content, /is not answering/, value);
			assert.match(reply.data.content, /wrong pin, a wrong SPI mode, or an unpowered DW3110/);
			assert.ok(reply.data.content.includes(cite(DEVID.citations[1]!)), value);
		}
	});

	it("refuses to diagnose a value the tree does not document", async () => {
		const key = await makeKey();
		const reply = await invoke(key, "decode-devid", [{ name: "value", value: "0xDEADBEEF" }]);
		assert.match(reply.data.content, /no known signature/i);
		assert.match(reply.data.content, /will not guess/);
		assert.match(reply.data.content, /\/help-me/);
		// The two readings it does know must not leak into the unknown answer as
		// a suggestion. Naming them as what IS documented is fine; concluding
		// one of them is not.
		assert.ok(!/is answering/.test(reply.data.content));
		assert.ok(!/is not answering/.test(reply.data.content));
	});

	it("says so when the input is not hex, without echoing it", async () => {
		const key = await makeKey();
		const reply = await invoke(key, "decode-devid", [
			{ name: "value", value: "@everyone look" },
		]);
		assert.match(reply.data.content, /not a hex value/);
		assert.ok(!reply.data.content.includes("@everyone"));
		assert.deepEqual(reply.data.allowed_mentions.parse, []);
	});
});

describe("/why", () => {
	it("answers every topic with a citation", async () => {
		const key = await makeKey();
		for (const topic of TOPICS) {
			const reply = await invoke(key, "why", [{ name: "topic", value: topic.id }]);
			assert.match(reply.data.content, new RegExp(topic.label.replace(/[()]/g, "\\$&")), topic.id);
			assert.match(reply.data.content, /`[\w./-]+:\d+`/, `${topic.id} carries no file:line`);
			assert.equal(reply.data.flags, undefined, `${topic.id} should be public`);
		}
	});

	it("covers the minimum set the brief asked for", () => {
		const ids = TOPICS.map((t) => t.id);
		for (const required of [
			"no-serial-port",
			"no-shell",
			"hang-on-release",
			"two-probes",
			"nfc-no-distance",
			"agent-holds-port",
		]) {
			assert.ok(ids.includes(required), `missing topic ${required}`);
		}
	});

	it("lists what it knows rather than guessing at an unknown topic", async () => {
		const key = await makeKey();
		const reply = await invoke(key, "why", [{ name: "topic", value: "made-up-topic" }]);
		assert.match(reply.data.content, /do not have an entry/);
		assert.match(reply.data.content, /no-serial-port/);
		assert.ok(!reply.data.content.includes("made-up-topic"));
	});

	it("stays inside Discord's message limit", async () => {
		const key = await makeKey();
		for (const topic of TOPICS) {
			const reply = await invoke(key, "why", [{ name: "topic", value: topic.id }]);
			assert.ok(reply.data.content.length <= 2000, `${topic.id} is too long`);
		}
	});
});
