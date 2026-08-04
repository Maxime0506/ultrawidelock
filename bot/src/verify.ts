/**
 * @file Ed25519 verification of Discord interaction requests.
 *
 * Nothing in this Worker may parse a request body before this file has
 * accepted it. Discord enforces that from the outside: when an interactions
 * URL is saved it POSTs a deliberately invalid PING, and refuses the URL if
 * the endpoint answers instead of rejecting. So the ordering is not a
 * preference, it is the thing that makes the endpoint installable at all.
 *
 * Fails closed everywhere. A malformed header, a wrong-length key, an
 * unsupported curve and a genuinely bad signature all return false, and the
 * caller turns every one of them into the same 401.
 */

const KEY_BYTES = 32;
const SIG_BYTES = 64;

/**
 * Imported keys are reused across requests within a Worker isolate. The key
 * material is public and the import is pure, so caching it costs nothing and
 * removes a subtle-crypto call from the hot path of every interaction.
 */
const keyCache = new Map<string, CryptoKey>();

/** Decode exactly `expected` bytes of hex, or null. Rejects odd lengths and
 *  non-hex characters outright: parseInt would read "0x" as 0. */
function hexToBytes(hex: string, expected: number): Uint8Array | null {
	if (hex.length !== expected * 2) return null;
	if (!/^[0-9a-fA-F]+$/.test(hex)) return null;
	const out = new Uint8Array(expected);
	for (let i = 0; i < expected; i++) {
		out[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
	}
	return out;
}

/** Import a hex-encoded raw Ed25519 public key, or null if it is unusable. */
async function importPublicKey(publicKeyHex: string): Promise<CryptoKey | null> {
	const cached = keyCache.get(publicKeyHex);
	if (cached) return cached;

	const raw = hexToBytes(publicKeyHex, KEY_BYTES);
	if (!raw) return null;

	try {
		const key = await crypto.subtle.importKey("raw", raw, { name: "Ed25519" }, false, [
			"verify",
		]);
		keyCache.set(publicKeyHex, key);
		return key;
	} catch {
		return null;
	}
}

/**
 * True only if `signatureHex` is a valid Ed25519 signature by `publicKeyHex`
 * over `timestamp + rawBody`, which is the exact string Discord signs.
 *
 * `rawBody` must be the untouched request text. Re-serialising parsed JSON
 * changes the bytes and the signature no longer verifies.
 */
export async function verifySignature(
	rawBody: string,
	signatureHex: string | null,
	timestamp: string | null,
	publicKeyHex: string,
): Promise<boolean> {
	if (!signatureHex || !timestamp || !publicKeyHex) return false;

	const signature = hexToBytes(signatureHex, SIG_BYTES);
	if (!signature) return false;

	const key = await importPublicKey(publicKeyHex);
	if (!key) return false;

	const message = new TextEncoder().encode(timestamp + rawBody);

	try {
		return await crypto.subtle.verify({ name: "Ed25519" }, key, signature, message);
	} catch {
		return false;
	}
}
