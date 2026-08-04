import { test } from "node:test";
import assert from "node:assert/strict";
import { advanceToGithubLeg, beginDiscordLeg, consumeGithubLeg } from "../src/oauthState.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";

const NOW = 1_000_000;
const USER = "222222222222222222";

test("the full happy path: begin -> advance -> consume returns the same Discord user id", async () => {
	const d1 = makeD1();
	try {
		const state = await beginDiscordLeg(d1.binding as never, NOW);
		assert.ok(state.length > 0);

		const advanced = await advanceToGithubLeg(d1.binding as never, state, USER, NOW + 1_000);
		assert.equal(advanced, true);

		const consumed = await consumeGithubLeg(d1.binding as never, state, NOW + 2_000);
		assert.equal(consumed, USER);
	} finally {
		d1.close();
	}
});

test("a state is consumed exactly once: the second consume returns null", async () => {
	const d1 = makeD1();
	try {
		const state = await beginDiscordLeg(d1.binding as never, NOW);
		await advanceToGithubLeg(d1.binding as never, state, USER, NOW);
		await consumeGithubLeg(d1.binding as never, state, NOW);
		assert.equal(await consumeGithubLeg(d1.binding as never, state, NOW), null, "already deleted");
	} finally {
		d1.close();
	}
});

test("advanceToGithubLeg cannot run twice on the same state", async () => {
	const d1 = makeD1();
	try {
		const state = await beginDiscordLeg(d1.binding as never, NOW);
		assert.equal(await advanceToGithubLeg(d1.binding as never, state, USER, NOW), true);
		assert.equal(await advanceToGithubLeg(d1.binding as never, state, "someone-else", NOW), false, "already staged github");
	} finally {
		d1.close();
	}
});

test("consumeGithubLeg on a state still stuck at the discord stage (skipped the Discord leg) returns null", async () => {
	const d1 = makeD1();
	try {
		const state = await beginDiscordLeg(d1.binding as never, NOW);
		assert.equal(await consumeGithubLeg(d1.binding as never, state, NOW), null);
	} finally {
		d1.close();
	}
});

test("an unknown state advances and consumes to nothing", async () => {
	const d1 = makeD1();
	try {
		assert.equal(await advanceToGithubLeg(d1.binding as never, "no-such-state", USER, NOW), false);
		assert.equal(await consumeGithubLeg(d1.binding as never, "no-such-state", NOW), null);
	} finally {
		d1.close();
	}
});

test("a state older than the TTL cannot be advanced or consumed", async () => {
	const d1 = makeD1();
	try {
		const state = await beginDiscordLeg(d1.binding as never, NOW);
		const wayLater = NOW + 60 * 60 * 1000; // 1 hour, well past the 10-minute TTL
		assert.equal(await advanceToGithubLeg(d1.binding as never, state, USER, wayLater), false);
	} finally {
		d1.close();
	}
});

test("degrades to OAuthStateUnavailable rather than throwing a raw D1 error", async () => {
	await assert.rejects(() => beginDiscordLeg(brokenD1() as never, NOW), { name: "OAuthStateUnavailable" });
});
