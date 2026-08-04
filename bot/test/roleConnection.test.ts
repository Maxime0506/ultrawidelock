import { test } from "node:test";
import assert from "node:assert/strict";
import { computeRegistryMetadata, METADATA_RECORDS, stringifyMetadata } from "../src/roleConnection.ts";
import { upsertRig } from "../src/rigs.ts";
import { recordValidation } from "../src/validations.ts";
import { makeD1 } from "./d1-sqlite.ts";

const USER = "999888777666555444";

test("METADATA_RECORDS has exactly 5 entries (Discord's own maximum) with distinct keys", () => {
	assert.equal(METADATA_RECORDS.length, 5);
	assert.equal(new Set(METADATA_RECORDS.map((r) => r.key)).size, 5);
});

test("computeRegistryMetadata on an unregistered user is all zero/false", async () => {
	const d1 = makeD1();
	try {
		const m = await computeRegistryMetadata(d1.binding as never, USER);
		assert.deepEqual(m, { boards_owned: 0, has_nfc: false, ios_major: 0, validated_runs: 0 });
	} finally {
		d1.close();
	}
});

test("computeRegistryMetadata counts boards, detects NFC, takes the highest iOS major, and counts validations", async () => {
	const d1 = makeD1();
	try {
		await upsertRig(d1.binding as never, {
			discord_user_id: USER,
			board: "esp32c6",
			radio: "dw3220",
			nfc: "none",
			phone_model: "iPhone 15",
			ios_version: "18.4",
			utc_offset: 0,
			awake_start: 8,
			awake_end: 22,
		});
		await upsertRig(d1.binding as never, {
			discord_user_id: USER,
			board: "nrf5340dk",
			radio: "dw3220",
			nfc: "st25r300",
			phone_model: "iPhone 16",
			ios_version: "19.1.2",
			utc_offset: 0,
			awake_start: 8,
			awake_end: 22,
		});
		await recordValidation(d1.binding as never, {
			id: "v1",
			board: "esp32c6",
			iosVersion: "18.4",
			passed: true,
			testedBy: USER,
			requestId: null,
			testedAt: 1_000,
		});
		await recordValidation(d1.binding as never, {
			id: "v2",
			board: "nrf5340dk",
			iosVersion: "19.1.2",
			passed: false,
			testedBy: USER,
			requestId: null,
			testedAt: 2_000,
		});

		const m = await computeRegistryMetadata(d1.binding as never, USER);
		assert.deepEqual(m, { boards_owned: 2, has_nfc: true, ios_major: 19, validated_runs: 2 });
	} finally {
		d1.close();
	}
});

test("stringifyMetadata stringifies every field, booleans as '1'/'0'", () => {
	assert.deepEqual(
		stringifyMetadata({ boards_owned: 3, has_nfc: true, ios_major: 19, validated_runs: 4, merged_prs: 2 }),
		{ boards_owned: "3", has_nfc: "1", ios_major: "19", validated_runs: "4", merged_prs: "2" },
	);
	assert.deepEqual(
		stringifyMetadata({ boards_owned: 0, has_nfc: false, ios_major: 0, validated_runs: 0, merged_prs: 0 }),
		{ boards_owned: "0", has_nfc: "0", ios_major: "0", validated_runs: "0", merged_prs: "0" },
	);
});
