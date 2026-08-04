/**
 * @file `/size`.
 *
 * The property that matters: the numbers it prints must be the ones actually
 * recorded in firmware/size-baseline.json. size-baseline.test.ts checks the
 * generated extract against a fresh one; this checks the command's reply
 * against the extract, so the two together cover source-to-reply.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { primaryBaseline } from "../src/size-baseline.ts";
import { interactionRequest, makeExecutionContext, makeKey, signBody } from "./helpers.ts";

async function invoke(): Promise<string> {
	const key = await makeKey();
	const ts = "1700000000";
	const body = JSON.stringify({ id: "i1", type: 2, data: { name: "size" } });
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": ts,
		"x-signature-ed25519": await signBody(key, ts, body),
	});
	const { ctx } = makeExecutionContext();
	const res = await worker.fetch(req, { DISCORD_PUBLIC_KEY: key.publicKeyHex } as Env, ctx);
	const payload = (await res.json()) as { data: { content: string } };
	return payload.data.content;
}

describe("/size", () => {
	it("reports the real recorded flash and RAM figures", async () => {
		const b = primaryBaseline()!;
		const reply = await invoke();
		assert.match(reply, new RegExp(b.regions.FLASH.used.toLocaleString()));
		assert.match(reply, new RegExp(`${b.regions.FLASH.pct}%`));
		assert.match(reply, new RegExp(b.commit.slice(0, 12)));
	});

	it("says it is not a live measurement", async () => {
		assert.match(await invoke(), /Not live/);
	});

	it("cites how the baseline is regenerated", async () => {
		assert.match(await invoke(), /mk\/cdk\.mk/);
	});
});
