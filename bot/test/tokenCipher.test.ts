import { test } from "node:test";
import assert from "node:assert/strict";
import { decryptToken, encryptToken, TokenCipherError } from "../src/tokenCipher.ts";

function randomKey(): string {
	return Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString("base64");
}

test("encryptToken then decryptToken round-trips the original plaintext", async () => {
	const key = randomKey();
	const blob = await encryptToken("gho_supersecrettoken", key);
	assert.equal(await decryptToken(blob, key), "gho_supersecrettoken");
});

test("the encrypted blob never contains the plaintext token as a substring", async () => {
	const key = randomKey();
	const blob = await encryptToken("gho_supersecrettoken", key);
	assert.doesNotMatch(blob, /gho_supersecrettoken/);
});

test("two encryptions of the same plaintext produce different blobs (fresh IV every call)", async () => {
	const key = randomKey();
	const a = await encryptToken("same-plaintext", key);
	const b = await encryptToken("same-plaintext", key);
	assert.notEqual(a, b);
	assert.equal(await decryptToken(a, key), "same-plaintext");
	assert.equal(await decryptToken(b, key), "same-plaintext");
});

test("decrypting with the wrong key fails rather than returning garbage plaintext", async () => {
	const blob = await encryptToken("secret", randomKey());
	await assert.rejects(() => decryptToken(blob, randomKey()), { name: "TokenCipherError" });
});

test("decrypting tampered ciphertext fails (GCM authenticates the data)", async () => {
	const key = randomKey();
	const blob = await encryptToken("secret", key);
	const bytes = Buffer.from(blob, "base64");
	bytes[bytes.length - 1] = bytes[bytes.length - 1]! ^ 0xff; // flip the last ciphertext byte
	const tampered = bytes.toString("base64");
	await assert.rejects(() => decryptToken(tampered, key), { name: "TokenCipherError" });
});

test("a key that is not 32 bytes after base64-decoding is a clear config error, not a crash", async () => {
	const shortKey = Buffer.from("too short").toString("base64");
	await assert.rejects(() => encryptToken("x", shortKey), { name: "TokenCipherError" });
});

test("a key that is not valid base64 is a clear config error", async () => {
	await assert.rejects(() => encryptToken("x", "not valid base64!!"), (err: unknown) => err instanceof TokenCipherError);
});
