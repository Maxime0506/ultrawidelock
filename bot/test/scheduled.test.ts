import { test } from "node:test";
import assert from "node:assert/strict";
import { DEFAULT_ESCALATE_MINUTES, escalateMinutesFrom, runEscalationSweep } from "../src/scheduled.ts";
import { createTestRequest, claim } from "../src/testRequests.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";
import { captureFollowups } from "./helpers.ts";

const ESCALATE_MINUTES = 30;
const NOW = 1_000_000_000;
const OLD_ENOUGH = NOW - (ESCALATE_MINUTES + 5) * 60_000;
const TOO_RECENT = NOW - 5 * 60_000;

async function seed(db: unknown, overrides: Partial<Parameters<typeof createTestRequest>[1]> = {}) {
	await createTestRequest(db as never, {
		id: "req-1",
		requesterId: "maintainer-1",
		board: "esp32c6",
		iosVersion: "19.1",
		what: "check approach unlock",
		channelId: "chan-1",
		messageId: "card-1",
		createdAt: OLD_ENOUGH,
		candidates: [
			{ discordUserId: "awake-1", awake: true },
			{ discordUserId: "asleep-1", awake: false },
			{ discordUserId: "asleep-2", awake: false },
		],
		...overrides,
	});
}

test("escalateMinutesFrom falls back to the documented default when unset or garbage", () => {
	assert.equal(escalateMinutesFrom({}), DEFAULT_ESCALATE_MINUTES);
	assert.equal(escalateMinutesFrom({ TEST_REQUEST_ESCALATE_MINUTES: "not a number" }), DEFAULT_ESCALATE_MINUTES);
	assert.equal(escalateMinutesFrom({ TEST_REQUEST_ESCALATE_MINUTES: "-5" }), DEFAULT_ESCALATE_MINUTES);
	assert.equal(escalateMinutesFrom({ TEST_REQUEST_ESCALATE_MINUTES: "45" }), 45);
});

test("sweep with no bot token bound does nothing and does not throw", async () => {
	const d1 = makeD1();
	try {
		await seed(d1.binding);
		const result = await runEscalationSweep(d1.binding as never, undefined, "corr-1", NOW, ESCALATE_MINUTES);
		assert.deepEqual(result, { requestsChecked: 0, requestsEscalated: 0, candidatesPinged: 0 });
	} finally {
		d1.close();
	}
});

test("sweep pings only the asleep candidates on a request past the window, and marks it escalated", async () => {
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seed(d1.binding);
		const result = await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);

		assert.equal(result.requestsChecked, 1);
		assert.equal(result.requestsEscalated, 1);
		assert.equal(result.candidatesPinged, 2);

		const pingCalls = followups.calls.filter((c) => c.url.includes("/channels/chan-1/messages") && c.method === "POST");
		assert.equal(pingCalls.length, 1);
		assert.match(pingCalls[0]!.body.content ?? "", /<@asleep-1>/);
		assert.match(pingCalls[0]!.body.content ?? "", /<@asleep-2>/);
		assert.doesNotMatch(pingCalls[0]!.body.content ?? "", /<@awake-1>/);

		const rows = d1.rows(`SELECT escalated_at FROM test_requests WHERE id = 'req-1'`);
		assert.equal(rows[0]!.escalated_at, NOW);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("sweep skips a request that has not been pending long enough", async () => {
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seed(d1.binding, { createdAt: TOO_RECENT });
		const result = await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);
		assert.equal(result.requestsChecked, 0);
		assert.equal(followups.calls.length, 0);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("sweep skips an already-claimed request even if it is old", async () => {
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seed(d1.binding);
		await claim(d1.binding as never, "req-1", "awake-1");
		const result = await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);
		assert.equal(result.requestsChecked, 0);
		assert.equal(followups.calls.length, 0);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("a request with only awake candidates is marked escalated without posting an empty ping", async () => {
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seed(d1.binding, {
			id: "req-2",
			candidates: [{ discordUserId: "awake-1", awake: true }],
		});
		const result = await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);
		assert.equal(result.requestsEscalated, 1);
		assert.equal(result.candidatesPinged, 0);
		assert.equal(followups.calls.length, 0, "nothing to ping means no message posted");

		const rows = d1.rows(`SELECT escalated_at FROM test_requests WHERE id = 'req-2'`);
		assert.equal(rows[0]!.escalated_at, NOW, "still marked escalated so it is not rechecked forever");
	} finally {
		followups.restore();
		d1.close();
	}
});

test("a second sweep tick does not re-ping the same candidates", async () => {
	const d1 = makeD1();
	const followups = captureFollowups();
	try {
		await seed(d1.binding);
		await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);
		followups.calls.length = 0;

		const secondResult = await runEscalationSweep(d1.binding as never, "bot-token", "corr-1", NOW + 5 * 60_000, ESCALATE_MINUTES);
		assert.equal(secondResult.requestsChecked, 0, "already escalated, so it drops out of the pending-unescalated set");
		assert.equal(followups.calls.length, 0);
	} finally {
		followups.restore();
		d1.close();
	}
});

test("degrades to an empty result rather than throwing when D1 is unreachable", async () => {
	const result = await runEscalationSweep(brokenD1() as never, "bot-token", "corr-1", NOW, ESCALATE_MINUTES);
	assert.deepEqual(result, { requestsChecked: 0, requestsEscalated: 0, candidatesPinged: 0 });
});
