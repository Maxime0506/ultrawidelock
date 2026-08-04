/**
 * @file AES-256-GCM for the one thing this bot stores that is a real bearer
 * credential rather than an opaque Discord user ID: the OAuth access and
 * refresh tokens Linked Roles needs to push updated metadata later without
 * re-prompting the user. D1 has no column-level encryption of its own, so
 * this exists to keep those tokens unreadable from a raw table dump —
 * `OAUTH_ENCRYPTION_KEY` is a Worker secret, never a D1 value.
 */

const KEY_BYTES = 32;
const IV_BYTES = 12;

export class TokenCipherError extends Error {
	constructor(message: string) {
		super(message);
		this.name = "TokenCipherError";
	}
}

const keyCache = new Map<string, CryptoKey>();

function base64ToBytes(b64: string): Uint8Array {
	const bin = atob(b64);
	const out = new Uint8Array(bin.length);
	for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
	return out;
}

function bytesToBase64(bytes: Uint8Array): string {
	let bin = "";
	for (const b of bytes) bin += String.fromCharCode(b);
	return btoa(bin);
}

async function deriveKey(secretBase64: string): Promise<CryptoKey> {
	const cached = keyCache.get(secretBase64);
	if (cached) return cached;

	let raw: Uint8Array;
	try {
		raw = base64ToBytes(secretBase64);
	} catch {
		throw new TokenCipherError("OAUTH_ENCRYPTION_KEY is not valid base64");
	}
	if (raw.length !== KEY_BYTES) {
		throw new TokenCipherError(`OAUTH_ENCRYPTION_KEY must decode to exactly ${KEY_BYTES} bytes, got ${raw.length}`);
	}

	const key = await crypto.subtle.importKey("raw", raw, { name: "AES-GCM" }, false, ["encrypt", "decrypt"]);
	keyCache.set(secretBase64, key);
	return key;
}

/** `iv || ciphertext`, both base64-joined as one string, so storage is one
 *  D1 column rather than two. A fresh random IV every call — AES-GCM must
 *  never reuse an IV under the same key. */
export async function encryptToken(plaintext: string, secretBase64: string): Promise<string> {
	const key = await deriveKey(secretBase64);
	const iv = crypto.getRandomValues(new Uint8Array(IV_BYTES));
	const ciphertext = await crypto.subtle.encrypt({ name: "AES-GCM", iv }, key, new TextEncoder().encode(plaintext));
	const combined = new Uint8Array(iv.length + ciphertext.byteLength);
	combined.set(iv, 0);
	combined.set(new Uint8Array(ciphertext), iv.length);
	return bytesToBase64(combined);
}

/** Throws TokenCipherError on a wrong key, corrupted data, or a mismatched
 *  auth tag — GCM authenticates the ciphertext, so tampering is detected
 *  rather than silently producing garbage plaintext. */
export async function decryptToken(blob: string, secretBase64: string): Promise<string> {
	const key = await deriveKey(secretBase64);
	let combined: Uint8Array;
	try {
		combined = base64ToBytes(blob);
	} catch {
		throw new TokenCipherError("stored token is not valid base64");
	}
	if (combined.length <= IV_BYTES) {
		throw new TokenCipherError("stored token is too short to contain an IV and ciphertext");
	}
	const iv = combined.slice(0, IV_BYTES);
	const ciphertext = combined.slice(IV_BYTES);
	try {
		const plaintext = await crypto.subtle.decrypt({ name: "AES-GCM", iv }, key, ciphertext);
		return new TextDecoder().decode(plaintext);
	} catch {
		throw new TokenCipherError("token decryption failed (wrong key or corrupted/tampered data)");
	}
}
