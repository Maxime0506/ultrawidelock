/**
 * @file `/context`.
 *
 * The one thing this must never do is claim to know something about the
 * caller's machine. Every case checks that the blanks stay blanks.
 */
import { strict as assert } from "node:assert";
import { afterEach, beforeEach, describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { block } from "../src/commands/context.ts";
import {
	captureFollowups,
	interactionRequest,
	makeExecutionContext,
	makeKey,
	signBody,
	type Followup,
	type TestKey,
} from "./helpers.ts";
import { brokenD1, makeD1, type FakeD1 } from "./d1-sqlite.ts";

const TS = "1700000000";
const USER = "222222222222222222";

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
	return { DISCORD_PUBLIC_KEY: key.publicKeyHex, DB: d1.binding, ...overrides } as Env;
}

async function invoke(e: Env): Promise<string | undefined> {
	const body = JSON.stringify({
		id: "i1",
		application_id: "app1",
		token: "tok1",
		type: 2,
		data: { name: "context" },
		member: { user: { id: USER } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx, settled } = makeExecutionContext();
	await worker.fetch(req, e, ctx);
	await settled();
	return cap.calls.filter((c) => c.method === "PATCH").at(-1)?.body.content;
}

describe("block", () => {
	it("never claims to know the host OS, NCS version or commit", () => {
		const text = block([], false);
		assert.match(text, /host OS: <fill in>/);
		assert.match(text, /NCS version installed[^:]*: <fill in>/);
		assert.match(text, /firmware commit[^:]*: <fill in>/);
	});

	it("cites the repo's own NCS pin", () => {
		assert.match(block([], false), /repo NCS pin: v3\.3\.0/);
		assert.match(block([], false), /Makefile:42/);
	});

	it("lists registered hardware when there is some", () => {
		assert.match(block(["DWM3001CDK · iPhone 15 Pro"], false), /DWM3001CDK/);
	});

	it("says so, distinctly, for none registered vs. registry unreachable", () => {
		assert.match(block([], false), /none \(\/ihave/);
		assert.match(block([], true), /unreachable/);
	});
});

describe("/context end to end", () => {
	it("includes what was registered", async () => {
		d1.rows(
			"INSERT INTO rigs (discord_user_id, board, radio, nfc, phone_model, ios_version, utc_offset, awake_start, awake_end, updated_at) VALUES (?1,'dwm3001cdk','dw3110','none','iPhone 15 Pro','26.1',0,0,1440,1)",
			USER,
		);
		const reply = await invoke(env());
		assert.match(reply ?? "", /DWM3001CDK/);
		assert.match(reply ?? "", /iPhone 15 Pro/);
	});

	it("degrades when the registry is unreachable, rather than throwing", async () => {
		const reply = await invoke(env({ DB: brokenD1() as Env["DB"] }));
		assert.match(reply ?? "", /unreachable/);
	});
});
