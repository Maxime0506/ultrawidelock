/**
 * @file The endpoint contract.
 *
 * The load-bearing test here is "rejects before parsing": it sends a body that
 * is not JSON with a signature that does not verify, and requires a 401. A
 * handler that parsed first would answer 400, and Discord would refuse the
 * interactions URL on save.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import {
	corrupt,
	interactionRequest,
	makeExecutionContext,
	makeKey,
	signBody,
	type TestKey,
} from "./helpers.ts";

const TS = "1700000000";

/** Drive the Worker and let any deferred work finish before asserting. */
async function run(req: Request, e: Env): Promise<Response> {
	const { ctx, settled } = makeExecutionContext();
	const res = await worker.fetch(req, e, ctx);
	await settled();
	return res;
}

/** Build a correctly signed request, then let the caller break one part. */
async function signed(
	key: TestKey,
	body: string,
	mutate: (h: Record<string, string>) => void = () => {},
): Promise<Request> {
	const headers: Record<string, string> = {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	};
	mutate(headers);
	return interactionRequest(body, headers);
}

function env(key: TestKey | null): Env {
	return { DISCORD_PUBLIC_KEY: key ? key.publicKeyHex : "" } as Env;
}

describe("interactions endpoint", () => {
	it("answers a signed PING with PONG", async () => {
		const key = await makeKey();
		const res = await run(await signed(key, '{"type":1}'), env(key));
		assert.equal(res.status, 200);
		assert.deepEqual(await res.json(), { type: 1 });
	});

	it("rejects a corrupted signature before parsing the body", async () => {
		const key = await makeKey();
		// Not JSON. If verification ran second this would be a 400.
		const body = "this is not json at all";
		const req = await signed(key, body, (h) => {
			h["x-signature-ed25519"] = corrupt(h["x-signature-ed25519"]!);
		});
		const res = await run(req, env(key));
		assert.equal(res.status, 401);
		assert.equal(await res.text(), "invalid request signature");
	});

	it("rejects a missing signature header", async () => {
		const key = await makeKey();
		const req = await signed(key, '{"type":1}', (h) => {
			delete h["x-signature-ed25519"];
		});
		assert.equal((await run(req, env(key))).status, 401);
	});

	it("rejects a missing timestamp header", async () => {
		const key = await makeKey();
		const req = await signed(key, '{"type":1}', (h) => {
			delete h["x-signature-timestamp"];
		});
		assert.equal((await run(req, env(key))).status, 401);
	});

	it("rejects everything when the public key is not bound", async () => {
		const key = await makeKey();
		const res = await run(await signed(key, '{"type":1}'), env(null));
		assert.equal(res.status, 401);
	});

	it("rejects a body signed by the wrong key", async () => {
		const signer = await makeKey();
		const configured = await makeKey();
		const res = await run(await signed(signer, '{"type":1}'), env(configured));
		assert.equal(res.status, 401);
	});

	it("400s a signed body that is not JSON", async () => {
		const key = await makeKey();
		const res = await run(await signed(key, "not json"), env(key));
		assert.equal(res.status, 400);
	});

	// GET / is informational rather than a 405: the Linked Roles OAuth flow
	// puts real browser GET routes on this same Worker, so "not a POST" stopped
	// meaning "not for us" once the two halves merged.
	it("serves GET / as an informational page, not an interaction endpoint", async () => {
		const key = await makeKey();
		const res = await run(new Request("https://bot.example/", { method: "GET" }), env(key));
		assert.equal(res.status, 200);
		assert.match(await res.text(), /interaction/i);
	});

	it("runs /ping and answers ephemerally with mentions suppressed", async () => {
		const key = await makeKey();
		const body = JSON.stringify({
			id: "1",
			type: 2,
			token: "t",
			data: { name: "ping" },
			member: { user: { id: "42" } },
		});
		const res = await run(await signed(key, body), env(key));
		assert.equal(res.status, 200);
		const payload = (await res.json()) as {
			type: number;
			data: { content: string; flags: number; allowed_mentions: { parse: string[] } };
		};
		assert.equal(payload.type, 4);
		assert.equal(payload.data.flags, 64);
		assert.deepEqual(payload.data.allowed_mentions.parse, []);
		assert.match(payload.data.content, /^pong\./);
	});

	it("answers an unknown command without echoing its name", async () => {
		const key = await makeKey();
		const body = JSON.stringify({
			id: "1",
			type: 2,
			token: "t",
			data: { name: "@everyone" },
		});
		const res = await run(await signed(key, body), env(key));
		assert.equal(res.status, 200);
		const payload = (await res.json()) as {
			data: { content: string; flags: number; allowed_mentions: { parse: string[] } };
		};
		assert.equal(payload.data.flags, 64);
		assert.deepEqual(payload.data.allowed_mentions.parse, []);
		assert.ok(!payload.data.content.includes("@everyone"), "command name must not be echoed");
	});

	it("400s an interaction type it does not handle", async () => {
		const key = await makeKey();
		const res = await run(await signed(key, '{"id":"1","type":99}'), env(key));
		assert.equal(res.status, 400);
	});

	it("rejects an oversized body", async () => {
		const key = await makeKey();
		const body = "x".repeat(256 * 1024 + 1);
		const res = await run(await signed(key, body), env(key));
		assert.equal(res.status, 413);
	});
});
