/**
 * @file `/twin approach` and `/twin explain`.
 *
 * approach runs the real compiled firmware (twin.ts -> web-twin/twin.js +
 * src/twin.wasm) inside the same Worker fetch path Discord invokes, not a
 * mock — the only thing stubbed here is the outgoing follow-up PATCH.
 * explain is checked for exactly one thing: that it says what it cannot do,
 * immediately, without deferring or attempting a decode (see
 * src/commands/twin.ts's file header for why it cannot).
 */
import { strict as assert } from "node:assert";
import { afterEach, beforeEach, describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { interactionRequest, makeExecutionContext, makeKey, signBody, type TestKey } from "./helpers.ts";

const TS = "1700000000";

let key: TestKey;
let patches: { content: string }[];
let restoreFetch: () => void;

beforeEach(async () => {
	key = await makeKey();
	patches = [];
	const original = globalThis.fetch;
	globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
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
});

function env(): Env {
	return { DISCORD_PUBLIC_KEY: key.publicKeyHex } as Env;
}

async function invokeTwin(
	interactionId: string,
	subcommand: string,
	options: { name: string; value: unknown }[],
): Promise<{ status: number; deferred: boolean; content?: string }> {
	const body = JSON.stringify({
		id: interactionId,
		application_id: "app1",
		token: "tok1",
		type: 2,
		data: { name: "twin", options: [{ name: subcommand, options }] },
		member: { user: { id: "222222222222222222" } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	const res = await worker.fetch(req, env(), ctx);
	await settled();
	const payload = (await res.json()) as { type: number; data?: { content?: string } };
	const deferred = payload.type === 5;
	return {
		status: res.status,
		deferred,
		content: deferred ? patches.at(-1)?.content : payload.data?.content,
	};
}

describe("/twin approach", () => {
	it("defers, then reports a trust-gate decision with a citation", async () => {
		const { status, deferred, content } = await invokeTwin("twin-1", "approach", [
			{ name: "speed", value: 1.4 },
			{ name: "noise", value: "none" },
			{ name: "drops", value: 0 },
		]);
		assert.equal(status, 200);
		assert.equal(deferred, true, "/twin approach always defers");
		assert.match(content ?? "", /Trust gate/);
		assert.match(content ?? "", /modules\/woz_uwb\/src\/fira\/fira_session\.c/);
		assert.match(content ?? "", /PDoA|AoA|NFC Express|point-release/i, "states its coverage limits");
	});

	it("defaults speed, noise and drops when omitted", async () => {
		const { deferred, content } = await invokeTwin("twin-2", "approach", []);
		assert.equal(deferred, true);
		assert.match(content ?? "", /speed `1\.4 m\/s`/);
	});

	it("rejects a speed outside the slider bounds without deferring", async () => {
		const { deferred, content } = await invokeTwin("twin-3", "approach", [{ name: "speed", value: 99 }]);
		assert.equal(deferred, false);
		assert.match(content ?? "", /between/);
	});

	it("still completes at the slowest allowed speed (worst case for the round cap)", async () => {
		// 0.2 m/s over the fixed 8 m default start is ~201 rounds — nowhere near
		// MAX_ROUNDS=500, so every speed this command's slider bounds allow stays
		// under the cap. The cap itself is exercised directly against
		// runApproachScenario in twin-scenario.test.ts, since nothing reachable
		// through /twin approach's options can trigger it.
		const { content } = await invokeTwin("twin-4", "approach", [{ name: "speed", value: 0.2 }]);
		assert.match(content ?? "", /Trust gate/);
	});
});

describe("/twin explain", () => {
	it("answers immediately, naming exactly why it cannot decode", async () => {
		const { status, deferred, content } = await invokeTwin("twin-5", "explain", [
			{ name: "block", value: "deadbeef" },
		]);
		assert.equal(status, 200);
		assert.equal(deferred, false, "/twin explain never defers — it does no work");
		assert.match(content ?? "", /not implemented/i);
		assert.match(content ?? "", /ccc_shim_rx\.c/);
		assert.match(content ?? "", /CCM/);
	});
});
