/**
 * @file `/build`.
 *
 * The three things that must hold for the heaviest command this bot offers:
 * a retried delivery dispatches once, a cooldown blocks a second real request
 * and says how long is left, and the cooldown enforcement is the atomic
 * upsert in db.ts rather than a racy read-then-write (checked directly
 * against real SQLite in build-cooldown.test.ts, not here).
 */
import { strict as assert } from "node:assert";
import { afterEach, beforeEach, describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import {
	interactionRequest,
	makeExecutionContext,
	makeKey,
	signBody,
	type TestKey,
} from "./helpers.ts";
import { makeD1, type FakeD1 } from "./d1-sqlite.ts";

const TS = "1700000000";
const USER = "222222222222222222";

let key: TestKey;
let d1: FakeD1;
let dispatchCalls: string[];
let dispatchBodies: { ref: string; inputs: { targets: string; save_ccache?: string } }[];
let patches: { content: string }[];
let restoreFetch: () => void;

beforeEach(async () => {
	key = await makeKey();
	d1 = makeD1();
	dispatchCalls = [];
	dispatchBodies = [];
	patches = [];
	const original = globalThis.fetch;
	globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
		const url = String(input);
		if (url.includes("/dispatches")) {
			dispatchCalls.push(url);
			dispatchBodies.push(JSON.parse(String(init?.body ?? "{}")));
			return new Response(null, { status: 204 });
		}
		if (url.includes("/runs")) {
			return new Response(
				JSON.stringify({
					workflow_runs: [
						{
							html_url: "https://github.com/openaliro/openaliro/actions/runs/1",
							created_at: new Date().toISOString(),
						},
					],
				}),
				{ status: 200 },
			);
		}
		if (init?.method === "PATCH") {
			patches.push(JSON.parse(String(init.body ?? "{}")));
			return new Response("{}", { status: 200 });
		}
		return new Response("{}", { status: 200 });
	}) as typeof fetch;
	restoreFetch = () => void (globalThis.fetch = original);
});

afterEach(() => {
	restoreFetch();
	d1.close();
});

function env(overrides: Partial<Env> = {}): Env {
	return {
		DISCORD_PUBLIC_KEY: key.publicKeyHex,
		DB: d1.binding,
		GITHUB_ACTIONS_TOKEN: "not-a-real-token",
		...overrides,
	} as Env;
}

async function invoke(
	interactionId: string,
	e: Env,
	target = "dwm3001cdk",
): Promise<string | undefined> {
	const before = patches.length;
	const body = JSON.stringify({
		id: interactionId,
		application_id: "app1",
		token: "tok1",
		type: 2,
		data: { name: "build", options: [{ name: "target", value: target }] },
		member: { user: { id: USER } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const res = await worker.fetch(req, e, ctx);
	await settled();
	assert.equal(res.status, 200);
	const immediate = (await res.json()) as { type: number };
	assert.equal(immediate.type, 5, "/build always defers");
	return patches.slice(before).at(-1)?.content;
}

describe("/build", () => {
	it("dispatches and reports a run URL", async () => {
		const reply = await invoke("build-1", env());
		assert.equal(dispatchCalls.length, 1);
		assert.match(reply ?? "", /Dispatched/);
		assert.match(reply ?? "", /actions\/runs\/1/);
	});

	it("does not dispatch twice for a retried delivery", async () => {
		await invoke("build-retry", env());
		const second = await invoke("build-retry", env());
		assert.equal(dispatchCalls.length, 1);
		assert.match(second ?? "", /duplicate delivery/i);
	});

	it("blocks a second real request from the same user and says how long", async () => {
		await invoke("build-a", env());
		const second = await invoke("build-b", env());
		assert.equal(dispatchCalls.length, 1, "the second request must not dispatch");
		assert.match(second ?? "", /cooldown/i);
		assert.match(second ?? "", /\d+ minutes?|less than a minute/);
	});

	it("lets a different user through immediately", async () => {
		await invoke("build-a", env());
		const body = JSON.stringify({
			id: "build-c",
			application_id: "app1",
			token: "tok1",
			type: 2,
			data: { name: "build", options: [{ name: "target", value: "dwm3001cdk" }] },
			member: { user: { id: "333333333333333333" } },
		});
		const req = interactionRequest(body, {
			"content-type": "application/json",
			"x-signature-timestamp": TS,
			"x-signature-ed25519": await signBody(key, TS, body),
		});
		const { ctx, settled } = makeExecutionContext();
		const res = await worker.fetch(req, env(), ctx);
		await settled();
		assert.equal(res.status, 200);
		assert.equal(dispatchCalls.length, 2);
	});

	it("does not dispatch when the token is not bound", async () => {
		const reply = await invoke("build-1", env({ GITHUB_ACTIONS_TOKEN: undefined }));
		assert.equal(dispatchCalls.length, 0);
		assert.match(reply ?? "", /Could not dispatch/);
	});

	it("passes the requested target through as the workflow input", async () => {
		await invoke("build-1", env(), "esp32-matter");
		assert.equal(dispatchBodies[0]?.inputs.targets, "esp32-matter");
		assert.equal(dispatchBodies[0]?.ref, "main");
	});

	it("always dispatches with save_ccache false, to stay restore-only", async () => {
		await invoke("build-1", env());
		assert.equal(dispatchBodies[0]?.inputs.save_ccache, "false");
	});

	it("rejects an unknown target without dispatching or deferring", async () => {
		const body = JSON.stringify({
			id: "build-bad",
			application_id: "app1",
			token: "tok1",
			type: 2,
			data: { name: "build", options: [{ name: "target", value: "raspberry-pi" }] },
			member: { user: { id: USER } },
		});
		const req = interactionRequest(body, {
			"content-type": "application/json",
			"x-signature-timestamp": TS,
			"x-signature-ed25519": await signBody(key, TS, body),
		});
		const { ctx, settled } = makeExecutionContext();
		const res = await worker.fetch(req, env(), ctx);
		await settled();
		const payload = (await res.json()) as { type: number; data: { content: string } };
		assert.equal(payload.type, 4, "answers immediately, no defer");
		assert.match(payload.data.content, /not a target/);
		assert.equal(dispatchCalls.length, 0);
	});
});
