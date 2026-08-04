/**
 * @file The matcher.
 *
 * The failure this file exists to catch is a pattern that is loose enough to
 * answer a question it was not written for. A signature that never fires is a
 * gap somebody notices; a signature that fires on the wrong paste is an
 * evening somebody loses.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import { SIGNATURES, matchSignatures } from "../src/signatures.ts";

function ids(text: string): string[] {
	return matchSignatures(text).map((m) => m.signature.id);
}

describe("the signature table", () => {
	it("has no duplicate ids", () => {
		const seen = SIGNATURES.map((s) => s.id);
		assert.equal(new Set(seen).size, seen.length);
	});

	it("uses no global regex", () => {
		// A /g pattern carries lastIndex between calls, so the same paste would
		// match on one invocation and miss on the next.
		for (const s of SIGNATURES) {
			assert.ok(!s.pattern.global, `${s.id} pattern is global`);
			assert.ok(!s.context?.global, `${s.id} context is global`);
		}
	});

	it("gives every signature a citation and a next step", () => {
		for (const s of SIGNATURES) {
			assert.ok(s.citations.length > 0, `${s.id} has no citation`);
			assert.ok(s.next.length > 0, `${s.id} has no next step`);
		}
	});
});

describe("matching", () => {
	it("finds the literal error strings", () => {
		assert.ok(ids("E: dwt_probe failed: -1").includes("dwt-probe-failed"));
		assert.ok(ids("protocol 0 unsupported").includes("protocol-0-unsupported"));
		assert.ok(ids("disconnected, reason 531").includes("disconnect-531"));
		assert.ok(ids("more than one debug probe is attached").includes("two-probes"));
		assert.ok(ids("AutoRelockTime is 5").includes("auto-relock"));
	});

	it("reads a dead DEV_ID in either order", () => {
		assert.ok(ids("DEV_ID 0x00000000").includes("devid-dead"));
		assert.ok(ids("read 0xFFFFFFFF for DEV_ID").includes("devid-dead"));
	});

	it("does not call a healthy DEV_ID dead", () => {
		assert.ok(!ids("DEV_ID 0xDECA0302").includes("devid-dead"));
	});

	it("returns nothing for a paste it does not know", () => {
		assert.deepEqual(ids("everything is fine, no errors at all"), []);
	});

	it("ranks the more specific match first", () => {
		const ranked = ids("GeneralError URSK_Unavailable");
		assert.equal(ranked[0], "ursk-unavailable-m1");
		assert.ok(ranked.includes("ursk-unavailable-trigger"), "both causes are shown");
	});

	it("is stable across repeated calls", () => {
		const paste = "dwt_probe failed: -1 and protocol 0 unsupported";
		assert.deepEqual(ids(paste), ids(paste));
	});
});

describe("patterns that would otherwise over-match", () => {
	it("does not diagnose Matter bugs from an unrelated 'no response'", () => {
		assert.ok(!ids("no response from the debug probe").includes("no-response-tile"));
		assert.ok(!ids("the sensor gives No Response on the bus").includes("no-response-tile"));
	});

	it("still catches the real Home symptom", () => {
		assert.ok(ids("Matter Accessory / No Response").includes("no-response-tile"));
		assert.ok(ids("the Home tile says No Response").includes("no-response-tile"));
	});

	it("does not blame advertising for an unrelated -ENOMEM", () => {
		assert.ok(!ids("k_malloc returned -ENOMEM in the parser").includes("adv-enomem"));
	});

	it("still catches the advert restart failure", () => {
		assert.ok(ids("bt_le_adv_start() returned -12").includes("adv-enomem"));
		assert.ok(
			ids("re-advertising from disconnected callback: -ENOMEM").includes("adv-enomem"),
		);
	});
});
