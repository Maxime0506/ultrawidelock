import { test } from "node:test";
import assert from "node:assert/strict";
import {
	acceptCustomId,
	buildAwakePing,
	buildEscalationPing,
	buildTestRequestMessage,
	requestIdFromCustomId,
} from "../src/testRequestContainer.ts";
import { MessageFlags } from "../src/discord.ts";

const BASE = {
	requestId: "req-1",
	board: "esp32c6",
	iosVersion: "19.1",
	what: "check approach unlock",
	awakeCount: 2,
	asleepCount: 1,
	nextWakeUnixMs: 1_700_000_000_000,
};

test("acceptCustomId and requestIdFromCustomId round-trip", () => {
	const id = acceptCustomId("abc-123");
	assert.equal(id, "test-accept:abc-123");
	assert.equal(requestIdFromCustomId(id), "abc-123");
});

test("requestIdFromCustomId rejects anything not shaped like this bot's own Accept button", () => {
	assert.equal(requestIdFromCustomId("ihave:board:radio:nfc"), null);
	assert.equal(requestIdFromCustomId("test-accept:"), null);
	assert.equal(requestIdFromCustomId("test-accept:a:b"), null);
	assert.equal(requestIdFromCustomId("garbage"), null);
});

test("buildTestRequestMessage sets IS_COMPONENTS_V2 and a Container as the sole top-level component", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "pending" });
	assert.equal(body.flags, MessageFlags.IsComponentsV2);
	assert.equal(body.components.length, 1);
	const container = body.components[0] as { type: number; accent_color: number; components: unknown[] };
	assert.equal(container.type, 17);
	assert.equal(container.accent_color, 0xf1c40f);
});

test("pending state shows an enabled Accept button and the awake/asleep/next-wake status line", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "pending" });
	const container = body.components[0] as { components: unknown[] };
	const section = container.components[3]! as { components: { content: string }[]; accessory: { disabled: boolean; label: string; custom_id: string } };
	assert.match(section.components[0]!.content, /2 awake now/);
	assert.match(section.components[0]!.content, /1 asleep/);
	assert.match(section.components[0]!.content, /<t:1700000000:R>/);
	assert.equal(section.accessory.disabled, false);
	assert.equal(section.accessory.label, "Accept");
	assert.equal(section.accessory.custom_id, "test-accept:req-1");
});

test("claimed state disables the button and names the claimer, no asleep/awake counts", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "claimed", claimedBy: "user-42" });
	const container = body.components[0] as { accent_color: number; components: unknown[] };
	assert.equal(container.accent_color, 0x5865f2);
	const section = container.components[3]! as { components: { content: string }[]; accessory: { disabled: boolean; label: string } };
	assert.match(section.components[0]!.content, /CLAIMED/);
	assert.match(section.components[0]!.content, /<@user-42>/);
	assert.equal(section.accessory.disabled, true);
	assert.equal(section.accessory.label, "Claimed");
});

test("done+passed shows a green accent and a disabled Passed button", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "done", passed: true, claimedBy: "user-42" });
	const container = body.components[0] as { accent_color: number; components: unknown[] };
	assert.equal(container.accent_color, 0x2ecc71);
	const section = container.components[3]! as { components: { content: string }[]; accessory: { disabled: boolean; label: string } };
	assert.match(section.components[0]!.content, /PASSED/);
	assert.match(section.components[0]!.content, /<@user-42>/);
	assert.equal(section.accessory.disabled, true);
	assert.equal(section.accessory.label, "Passed");
});

test("done+failed shows a red accent and a disabled Failed button", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "done", passed: false, claimedBy: "user-42" });
	const container = body.components[0] as { accent_color: number; components: unknown[] };
	assert.equal(container.accent_color, 0xe74c3c);
	const section = container.components[3]! as { components: { content: string }[]; accessory: { disabled: boolean; label: string } };
	assert.match(section.components[0]!.content, /FAILED/);
	assert.equal(section.accessory.disabled, true);
	assert.equal(section.accessory.label, "Failed");
});

test("zero asleep candidates omits the next-wake clause entirely", () => {
	const body = buildTestRequestMessage({ ...BASE, status: "pending", asleepCount: 0, nextWakeUnixMs: null });
	const container = body.components[0] as { components: unknown[] };
	const section = container.components[3]! as { components: { content: string }[] };
	assert.doesNotMatch(section.components[0]!.content, /asleep/);
	assert.doesNotMatch(section.components[0]!.content, /next wakes/);
});

test("buildAwakePing and buildEscalationPing carry only the given IDs in the mention allow-list, never a bare parse-all", () => {
	const ping = buildAwakePing(["a", "b"]);
	assert.deepEqual(ping.allowed_mentions, { parse: [], users: ["a", "b"] });
	assert.match(ping.content, /<@a> <@b>/);

	const escalation = buildEscalationPing(["c"]);
	assert.deepEqual(escalation.allowed_mentions, { parse: [], users: ["c"] });
	assert.match(escalation.content, /<@c>/);
});
