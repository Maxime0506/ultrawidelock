import { test } from "node:test";
import assert from "node:assert/strict";
import {
	asleepUnpingedCandidates,
	claim,
	createTestRequest,
	getRequest,
	getRequestByThreadId,
	markDone,
	markEscalated,
	pendingUnescalated,
	setThread,
} from "../src/testRequests.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";

function newReq(overrides: Partial<Parameters<typeof createTestRequest>[1]> = {}) {
	return {
		id: "req-1",
		requesterId: "maintainer-1",
		board: "esp32c6",
		iosVersion: "19.1",
		what: "check approach unlock",
		channelId: "chan-1",
		messageId: "msg-orig",
		createdAt: 1_000,
		candidates: [
			{ discordUserId: "awake-1", awake: true },
			{ discordUserId: "asleep-1", awake: false },
		],
		...overrides,
	};
}

test("createTestRequest writes the request (with the message id already known) and every candidate atomically", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq());
		const req = await getRequest(d1.binding as never, "req-1");
		assert.ok(req);
		assert.equal(req.status, "pending");
		assert.equal(req.message_id, "msg-orig");

		const candidates = d1.rows(`SELECT discord_user_id, awake_at_request, pinged_awake FROM test_request_candidates WHERE request_id = ?`, "req-1");
		assert.equal(candidates.length, 2);
	} finally {
		d1.close();
	}
});

test("setThread fills in the thread id once someone accepts, the one field genuinely unknown at creation", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq());
		await setThread(d1.binding as never, "req-1", "thread-1");
		const req = await getRequest(d1.binding as never, "req-1");
		assert.equal(req?.thread_id, "thread-1");
	} finally {
		d1.close();
	}
});

test("claim is first-accept-wins: the second call on an already-claimed request returns false and changes nothing", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq());
		const first = await claim(d1.binding as never, "req-1", "user-a");
		const second = await claim(d1.binding as never, "req-1", "user-b");
		assert.equal(first, true);
		assert.equal(second, false);

		const req = await getRequest(d1.binding as never, "req-1");
		assert.equal(req?.status, "claimed");
		assert.equal(req?.claimed_by, "user-a");
	} finally {
		d1.close();
	}
});

test("claim on an unknown request id returns false rather than throwing", async () => {
	const d1 = makeD1();
	try {
		assert.equal(await claim(d1.binding as never, "does-not-exist", "user-a"), false);
	} finally {
		d1.close();
	}
});

test("pendingUnescalated finds only requests past the cutoff, still pending, not yet escalated", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq({ id: "old", createdAt: 1_000 }));
		await createTestRequest(d1.binding as never, newReq({ id: "new", createdAt: 9_000 }));
		await createTestRequest(d1.binding as never, newReq({ id: "claimed-old", createdAt: 1_000 }));
		await claim(d1.binding as never, "claimed-old", "user-a");

		const due = await pendingUnescalated(d1.binding as never, 5_000);
		assert.deepEqual(due.map((r) => r.id).sort(), ["old"]);
	} finally {
		d1.close();
	}
});

test("markEscalated marks the request and only the named candidates as pinged, so a partial ping list doesn't skip anyone next sweep", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(
			d1.binding as never,
			newReq({
				id: "req-2",
				candidates: [
					{ discordUserId: "asleep-1", awake: false },
					{ discordUserId: "asleep-2", awake: false },
				],
			}),
		);

		let due = await asleepUnpingedCandidates(d1.binding as never, "req-2");
		assert.deepEqual(due.sort(), ["asleep-1", "asleep-2"]);

		await markEscalated(d1.binding as never, "req-2", 5_000, ["asleep-1"]);

		due = await asleepUnpingedCandidates(d1.binding as never, "req-2");
		assert.deepEqual(due, ["asleep-2"]);

		const req = await getRequest(d1.binding as never, "req-2");
		assert.equal(req?.escalated_at, 5_000);
	} finally {
		d1.close();
	}
});

test("getRequestByThreadId finds a request by its thread once claimed, and finds nothing before that", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq());
		assert.equal(await getRequestByThreadId(d1.binding as never, "thread-1"), null);

		await setThread(d1.binding as never, "req-1", "thread-1");
		const found = await getRequestByThreadId(d1.binding as never, "thread-1");
		assert.equal(found?.id, "req-1");
	} finally {
		d1.close();
	}
});

test("markDone is an atomic claimed->done guard: only fires from 'claimed', and only once", async () => {
	const d1 = makeD1();
	try {
		await createTestRequest(d1.binding as never, newReq());

		assert.equal(await markDone(d1.binding as never, "req-1"), false, "still pending, not claimed");

		await claim(d1.binding as never, "req-1", "user-a");
		assert.equal(await markDone(d1.binding as never, "req-1"), true);
		assert.equal((await getRequest(d1.binding as never, "req-1"))?.status, "done");

		assert.equal(await markDone(d1.binding as never, "req-1"), false, "already done, second call is a no-op");
	} finally {
		d1.close();
	}
});

test("degrades to RoutingUnavailable rather than throwing a raw D1 error when the binding is broken", async () => {
	await assert.rejects(() => createTestRequest(brokenD1() as never, newReq()), { name: "RoutingUnavailable" });
});
