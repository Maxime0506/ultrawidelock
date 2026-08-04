/**
 * @file Signature verification, at the function level.
 *
 * index.test.ts covers the same ground through the HTTP surface. Both exist:
 * this one says which input class failed, that one says the endpoint refuses
 * to parse before it has checked.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import { verifySignature } from "../src/verify.ts";
import { corrupt, makeKey, signBody } from "./helpers.ts";

const TS = "1700000000";
const BODY = '{"type":1}';

describe("verifySignature", () => {
	it("accepts a genuine signature", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		assert.equal(await verifySignature(BODY, sig, TS, key.publicKeyHex), true);
	});

	it("rejects a corrupted signature", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		assert.equal(await verifySignature(BODY, corrupt(sig), TS, key.publicKeyHex), false);
	});

	it("rejects a signature over a different body", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		assert.equal(await verifySignature('{"type":2}', sig, TS, key.publicKeyHex), false);
	});

	it("rejects a signature over a different timestamp", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		assert.equal(await verifySignature(BODY, sig, "1700000001", key.publicKeyHex), false);
	});

	it("rejects a signature from another key", async () => {
		const signer = await makeKey();
		const other = await makeKey();
		const sig = await signBody(signer, TS, BODY);
		assert.equal(await verifySignature(BODY, sig, TS, other.publicKeyHex), false);
	});

	it("rejects missing headers", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		assert.equal(await verifySignature(BODY, null, TS, key.publicKeyHex), false);
		assert.equal(await verifySignature(BODY, sig, null, key.publicKeyHex), false);
		assert.equal(await verifySignature(BODY, sig, TS, ""), false);
	});

	it("rejects malformed hex without throwing", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		// wrong length, non-hex characters, and the "0x" case that parseInt
		// would silently read as zero
		for (const bad of [sig.slice(0, 126), sig + "00", "z".repeat(128), "0x".repeat(64)]) {
			assert.equal(await verifySignature(BODY, bad, TS, key.publicKeyHex), false, bad.slice(0, 8));
		}
	});

	it("rejects a malformed public key without throwing", async () => {
		const key = await makeKey();
		const sig = await signBody(key, TS, BODY);
		for (const bad of ["", "abcd", "z".repeat(64), key.publicKeyHex + "00"]) {
			assert.equal(await verifySignature(BODY, sig, TS, bad), false, bad.slice(0, 8));
		}
	});

	it("is sensitive to the exact bytes, not the parsed value", async () => {
		// Re-serialising a parsed body reorders nothing here but does change
		// whitespace, and the signature must not survive that.
		const spaced = '{ "type" : 1 }';
		const key = await makeKey();
		const sig = await signBody(key, TS, spaced);
		assert.equal(await verifySignature(spaced, sig, TS, key.publicKeyHex), true);
		assert.equal(
			await verifySignature(JSON.stringify(JSON.parse(spaced)), sig, TS, key.publicKeyHex),
			false,
		);
	});
});
