/**
 * @file `/help-me`, end to end.
 *
 * This is the command with the most ways to be quietly wrong, so the cases
 * that matter are the honesty ones: an unmatched paste must say so and ping
 * rather than produce a plausible reading, a retried delivery must not open a
 * second thread, and a dead D1 must cost the registry lookup and nothing else.
 */
import { strict as assert } from "node:assert";
import { afterEach, beforeEach, describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import {
	consoleBlock,
	contextBlock,
	parseModalId,
	threadName,
} from "../src/commands/help-me.ts";
import { matchSignatures, SIGNATURES } from "../src/signatures.ts";
import { cite } from "../src/citations.ts";
import {
	captureFollowups,
	FAKE_THREAD_ID,
	interactionRequest,
	makeExecutionContext,
	makeKey,
	signBody,
	threadCreations,
	threadMessages,
	type CaptureOptions,
	type Followup,
	type TestKey,
} from "./helpers.ts";
import { brokenD1, makeD1, type FakeD1 } from "./d1-sqlite.ts";

const TS = "1700000000";
const MAINTAINER = "111111111111111111";
const REPORTER = "222222222222222222";
const CHANNEL = "333000000000000001";

let key: TestKey;
let d1: FakeD1;
let cap: { calls: Followup[]; restore: () => void };

beforeEach(async () => {
	key = await makeKey();
	d1 = makeD1();
	cap = captureFollowups();
});

afterEach(() => {
	cap.restore();
	d1.close();
});

function env(overrides: Partial<Env> = {}): Env {
	return {
		DISCORD_PUBLIC_KEY: key.publicKeyHex,
		DB: d1.binding,
		MAINTAINER_IDS: MAINTAINER,
		DISCORD_BOT_TOKEN: "not-a-real-token",
		FORUM_CHANNELS: `dwm3001cdk=${CHANNEL}`,
		...overrides,
	} as Env;
}

async function send(body: string, e: Env): Promise<Response> {
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const res = await worker.fetch(req, e, ctx);
	await settled();
	return res;
}

async function openCommand(
	e: Env,
	options: { name: string; value: unknown }[],
): Promise<{ type: number; data: Record<string, unknown> }> {
	const res = await send(
		JSON.stringify({
			id: "cmd-1",
			application_id: "app1",
			token: "tok1",
			type: 2,
			data: { name: "help-me", options },
			member: { user: { id: REPORTER } },
		}),
		e,
	);
	return (await res.json()) as { type: number; data: Record<string, unknown> };
}

/**
 * Submit the modal and return what the user ends up reading. A rejected form
 * answers immediately; everything else defers and edits, so the text arrives
 * by two different routes and the caller should not have to care which.
 */
async function submit(
	e: Env,
	fields: { expected?: string; actual?: string; console?: string },
	opts: { customId?: string; interactionId?: string } = {},
): Promise<string | undefined> {
	const before = cap.calls.length;
	const components = Object.entries(fields).map(([custom_id, value]) => ({
		components: [{ custom_id, value, type: 4 }],
	}));
	const res = await send(
		JSON.stringify({
			id: opts.interactionId ?? "modal-1",
			application_id: "app1",
			token: "tok1",
			type: 5,
			data: { custom_id: opts.customId ?? "help-me|dwm3001cdk|build|0", components },
			member: { user: { id: REPORTER } },
		}),
		e,
	);

	const immediate = (await res.json()) as { type: number; data?: { content?: string } };
	if (immediate.type === 4) return immediate.data?.content;

	return cap.calls
		.slice(before)
		.filter((c) => c.method === "PATCH")
		.at(-1)?.body.content;
}

describe("/help-me the command", () => {
	it("opens a modal with the three free-text fields", async () => {
		const reply = await openCommand(env(), [
			{ name: "board", value: "dwm3001cdk" },
			{ name: "image", value: "build" },
		]);
		assert.equal(reply.type, 9, "type 9 is a modal");
		assert.equal(reply.data.custom_id, "help-me|dwm3001cdk|build|0");
		const rows = reply.data.components as { components: { custom_id: string }[] }[];
		assert.deepEqual(
			rows.map((r) => r.components[0]!.custom_id),
			["expected", "actual", "console"],
		);
	});

	it("carries a ping request through the modal id", async () => {
		const reply = await openCommand(env(), [
			{ name: "board", value: "dwm3001cdk" },
			{ name: "image", value: "build" },
			{ name: "ping_maintainer", value: true },
		]);
		assert.equal(reply.data.custom_id, "help-me|dwm3001cdk|build|1");
	});

	it("refuses a board that is not on the list", async () => {
		const reply = await openCommand(env(), [
			{ name: "board", value: "Raspberry Pi" },
			{ name: "image", value: "build" },
		]);
		assert.equal(reply.type, 4, "a message, not a modal");
	});
});

describe("parseModalId", () => {
	it("accepts a well-formed id", () => {
		assert.deepEqual(parseModalId("help-me|esp32c6|esp-build|1"), {
			board: "esp32c6",
			image: "esp-build",
			ping: true,
		});
	});

	it("rejects a wrong prefix, an unknown board and an unknown image", () => {
		assert.equal(parseModalId("other|dwm3001cdk|build|0"), null);
		assert.equal(parseModalId("help-me|Raspberry Pi|build|0"), null);
		assert.equal(parseModalId("help-me|dwm3001cdk|make-up|0"), null);
		assert.equal(parseModalId(""), null);
	});
});

describe("/help-me submission", () => {
	it("opens a thread and posts the console separately", async () => {
		await submit(env(), {
			expected: "approach unlock",
			actual: "nothing happens",
			console: "dwt_probe failed: -1",
		});

		const threads = threadCreations(cap.calls);
		assert.equal(threads.length, 1);
		assert.match(String(threads[0]!.url), new RegExp(`/channels/${CHANNEL}/threads$`));
		assert.match(threads[0]!.body.message!.content!, /dwt-probe-failed/);
		// Derived from the signature table, not transcribed: the drift gate keeps
		// that table honest and does not read this file.
		const sig = SIGNATURES.find((s) => s.id === "dwt-probe-failed")!;
		assert.ok(threads[0]!.body.message!.content!.includes(cite(sig.citations[0]!)));

		const posts = threadMessages(cap.calls);
		assert.equal(posts.length, 1);
		assert.match(posts[0]!.body.content!, /dwt_probe failed: -1/);
	});

	it("says no known signature and pings the maintainer", async () => {
		await submit(env(), {
			expected: "it works",
			actual: "an entirely novel thing nobody wrote down",
			console: "qqqq zzzz nothing familiar here",
		});

		const created = threadCreations(cap.calls)[0]!;
		assert.match(created.body.message!.content!, /No known signature/);
		assert.match(created.body.message!.content!, /escalating rather than guessing/);
		assert.match(created.body.message!.content!, new RegExp(`<@${MAINTAINER}>`));
		assert.deepEqual(created.body.message!.allowed_mentions, {
			parse: [],
			users: [MAINTAINER],
		});
	});

	it("does not ping when a signature matched", async () => {
		await submit(env(), { expected: "x", actual: "y", console: "dwt_probe failed: -1" });
		const created = threadCreations(cap.calls)[0]!;
		assert.deepEqual(created.body.message!.allowed_mentions, { parse: [] });
		assert.ok(!created.body.message!.content!.includes(`<@${MAINTAINER}>`));
	});

	it("pings on request even when a signature matched", async () => {
		await submit(
			env(),
			{ expected: "x", actual: "y", console: "dwt_probe failed: -1" },
			{ customId: "help-me|dwm3001cdk|build|1" },
		);
		const created = threadCreations(cap.calls)[0]!;
		assert.deepEqual(created.body.message!.allowed_mentions?.users, [MAINTAINER]);
	});

	it("does not let user text ping anybody", async () => {
		await submit(env(), {
			expected: "@everyone please look",
			actual: "@here as well <@999000000000000009>",
			console: "@everyone",
		});
		for (const call of cap.calls) {
			const mentions = call.body.allowed_mentions ?? call.body.message?.allowed_mentions;
			assert.deepEqual(mentions?.parse, [], call.url);
			// Only the maintainer may ever be in `users`, and never an ID a user typed.
			for (const id of mentions?.users ?? []) assert.equal(id, MAINTAINER);
		}
	});

	it("does not open a second thread on a retried delivery", async () => {
		const e = env();
		const fields = { expected: "x", actual: "y", console: "dwt_probe failed: -1" };
		await submit(e, fields, { interactionId: "retry-me" });
		const second = await submit(e, fields, { interactionId: "retry-me" });

		assert.equal(threadCreations(cap.calls).length, 1, "exactly one thread");
		assert.match(second ?? "", /already posted/i);
		assert.match(second ?? "", /duplicate delivery/i);
	});

	it("treats a different submission as a different report", async () => {
		const e = env();
		await submit(e, { expected: "a", actual: "b" }, { interactionId: "one" });
		await submit(e, { expected: "c", actual: "d" }, { interactionId: "two" });
		assert.equal(threadCreations(cap.calls).length, 2);
	});

	it("finds the reporter's own hardware past the search cap", async () => {
		// Fifty other people register the same board first. A lookup that
		// filtered a capped search in memory would miss the reporter here and
		// print nothing, which reads exactly like "never registered".
		const now = Date.now();
		for (let i = 0; i < 60; i++) {
			d1.rows(
				"INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at) VALUES (?1,'dwm3001cdk','dw3110','none','other','1.0',0,0,1440,?2)",
				`9${String(i).padStart(17, "0")}`,
				now + i,
			);
		}
		d1.rows(
			"INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at) VALUES (?1,'dwm3001cdk','dw3110','none','iPhone 15 Pro','26.1',0,0,1440,?2)",
			REPORTER,
			1,
		);

		await submit(env(), { expected: "x", actual: "y", console: "dwt_probe failed: -1" });
		const created = threadCreations(cap.calls)[0]!;
		assert.match(created.body.message!.content!, /iPhone 15 Pro/);
		assert.match(created.body.message!.content!, /iOS 26\.1/);
	});

	it("still opens the thread when D1 is unreachable", async () => {
		await submit(env({ DB: brokenD1() as Env["DB"] }), {
			expected: "x",
			actual: "y",
			console: "dwt_probe failed: -1",
		});

		const created = threadCreations(cap.calls)[0]!;
		assert.ok(created, "the thread is the part that must survive");
		assert.match(created.body.message!.content!, /dwt-probe-failed/);
		assert.match(created.body.message!.content!, /registry was unreachable/);
	});

	it("hands the block back when no forum channel is configured", async () => {
		const reply = await submit(env({ FORUM_CHANNELS: "" }), {
			expected: "x",
			actual: "y",
			console: "dwt_probe failed: -1",
		});
		assert.equal(threadCreations(cap.calls).length, 0);
		assert.match(reply ?? "", /No forum channel is configured/);
		assert.match(reply ?? "", /dwt-probe-failed/);
	});

	it("hands the block back with a correlation id when the thread call fails", async () => {
		cap.restore();
		cap = captureFollowups({ failThreadCreate: true } satisfies CaptureOptions);
		const reply = await submit(env(), {
			expected: "x",
			actual: "y",
			console: "dwt_probe failed: -1",
		});
		assert.match(reply ?? "", /could not open the thread/i);
		assert.match(reply ?? "", /Nothing was lost/);
		assert.match(reply ?? "", /[0-9a-f]{8}/);
	});

	it("refuses a modal id it did not issue", async () => {
		const reply = await submit(
			env(),
			{ expected: "x", actual: "y" },
			{ customId: "help-me|Raspberry Pi|build|0" },
		);
		assert.equal(threadCreations(cap.calls).length, 0);
		assert.match(reply ?? "", /did not carry a board and image I recognise/);
	});
});

describe("the context block", () => {
	const base = {
		selections: { board: "dwm3001cdk", image: "build", ping: false },
		userId: REPORTER,
		expected: "e",
		actual: "a",
		hardware: null,
		registryLost: false,
		consoleLength: 10,
		maintainer: undefined,
	};

	it("always says what is still unknown", () => {
		const block = contextBlock({ ...base, matches: matchSignatures("dwt_probe failed: -1") });
		assert.match(block, /Still unknown:/);
		assert.match(block, /host OS/);
		assert.match(block, /NCS version/);
		assert.match(block, /firmware commit/);
	});

	it("names a missing console paste as unknown rather than passing over it", () => {
		const block = contextBlock({ ...base, consoleLength: 0, matches: [] });
		assert.match(block, /console output \(none pasted\)/);
	});

	it("shows every match, ranked, when a paste has more than one", () => {
		const matches = matchSignatures("GeneralError URSK_Unavailable at M1");
		assert.ok(matches.length >= 2, "this paste has two documented causes");
		const block = contextBlock({ ...base, matches });
		assert.match(block, /Matched 2 known signatures/);
		// Ranked by how much text matched, so the specific one leads.
		assert.ok(
			block.indexOf("ursk-unavailable-m1") < block.indexOf("ursk-unavailable-trigger"),
			"the more specific signature must come first",
		);
	});

	it("keeps the unknowns and the ping when the matches do not fit", () => {
		// The bug this replaces: a trailing slice to 2000 dropped both, which is
		// exactly the content nobody notices is missing.
		const noisy = [
			"dwt_probe failed: -1",
			"GeneralError URSK_Unavailable",
			"protocol 0 unsupported",
			"reason 531",
			"MPU FAULT",
			"bt_le_adv_start -ENOMEM",
			"COULD NOT RUN",
			"AutoRelockTime",
		].join("\n");
		const block = contextBlock({
			...base,
			expected: "x".repeat(300),
			actual: "y".repeat(700),
			matches: matchSignatures(noisy),
			maintainer: MAINTAINER,
		});
		assert.ok(block.length <= 2000, `block was ${block.length}`);
		assert.match(block, /Still unknown:/);
		assert.match(block, new RegExp(`<@${MAINTAINER}>`));
	});

	it("says how many matches it dropped rather than dropping them quietly", () => {
		const noisy = [
			"dwt_probe failed: -1",
			"GeneralError URSK_Unavailable",
			"protocol 0 unsupported",
			"reason 531",
			"MPU FAULT",
			"bt_le_adv_start -ENOMEM",
			"COULD NOT RUN",
			"AutoRelockTime",
		].join("\n");
		const block = contextBlock({
			...base,
			expected: "x".repeat(300),
			actual: "y".repeat(700),
			matches: matchSignatures(noisy),
			maintainer: MAINTAINER,
		});
		assert.match(block, /further match\(es\) not shown/);
	});

	it("stays inside Discord's message limit with a full table of matches", () => {
		const noisy = [
			"dwt_probe failed: -1",
			"GeneralError URSK_Unavailable",
			"protocol 0 unsupported",
			"reason 531",
			"MPU FAULT",
			"-ENOMEM",
			"COULD NOT RUN",
		].join("\n");
		const block = contextBlock({
			...base,
			expected: "x".repeat(300),
			actual: "y".repeat(700),
			matches: matchSignatures(noisy),
		});
		assert.ok(block.length <= 2000, `block was ${block.length}`);
	});
});

describe("the console paste", () => {
	it("truncates and says so, with both numbers", () => {
		const block = consoleBlock("x".repeat(5000));
		assert.ok(block.length <= 2000);
		assert.match(block, /truncated to \d+ of 5000 characters/);
	});

	it("does not say truncated when it did not", () => {
		assert.ok(!consoleBlock("short").includes("truncated"));
	});

	it("cannot be broken out of with a fence", () => {
		const block = consoleBlock("before\n```\nescaped?\n```\nafter");
		// Exactly two real fences: the ones this function added.
		assert.equal(block.split("```").length - 1, 2);
	});
});

describe("the thread name", () => {
	it("leads with the board and stays inside 100 characters", () => {
		const name = threadName("dwm3001cdk", "expected", "z".repeat(500));
		assert.ok(name.startsWith("DWM3001CDK:"));
		assert.ok(name.length <= 100, `was ${name.length}`);
	});

	it("collapses newlines so it reads as one line in a channel list", () => {
		assert.ok(!threadName("esp32c6", "e", "one\ntwo\nthree").includes("\n"));
	});

	it("falls back to the expected field when actual is empty", () => {
		assert.match(threadName("esp32c6", "the bolt never moves", ""), /the bolt never moves/);
	});
});

describe("the thread id in the reply", () => {
	it("links the thread it opened", async () => {
		const reply = await submit(env(), { expected: "x", actual: "y" });
		assert.match(reply ?? "", new RegExp(`<#${FAKE_THREAD_ID}>`));
	});
});
