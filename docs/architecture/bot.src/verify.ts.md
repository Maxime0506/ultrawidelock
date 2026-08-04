<!-- generated documentation — edit the source, not this file -->
# `bot/src/verify.ts`

@file Ed25519 verification of Discord interaction requests.
Nothing in this Worker may parse a request body before this file has
accepted it. Discord enforces that from the outside: when an interactions
URL is saved it POSTs a deliberately invalid PING, and refuses the URL if
the endpoint answers instead of rejecting. So the ordering is not a
preference, it is the thing that makes the endpoint installable at all.
Fails closed everywhere. A malformed header, a wrong-length key, an
unsupported curve and a genuinely bad signature all return false, and the
caller turns every one of them into the same 401.

**used by** [`bot/src/index.ts`](index.ts.md)

## API

### `function hexToBytes(hex: string, expected: number): Uint8Array | null`
`bot/src/verify.ts:27`

Decode exactly `expected` bytes of hex, or null. Rejects odd lengths and
non-hex characters outright: parseInt would read "0x" as 0.

**called by** `importPublicKey`, `verifySignature`

### `async function importPublicKey(publicKeyHex: string): Promise<CryptoKey | null>`
`bot/src/verify.ts:38`

Import a hex-encoded raw Ed25519 public key, or null if it is unusable.

**called by** `verifySignature`  ·  **calls** `hexToBytes`

### `export async function verifySignature(rawBody: string, signatureHex: string | null, timestamp: string | null, publicKeyHex: string): Promise<boolean>`
`bot/src/verify.ts:63`

True only if `signatureHex` is a valid Ed25519 signature by `publicKeyHex`
over `timestamp + rawBody`, which is the exact string Discord signs.
`rawBody` must be the untouched request text. Re-serialising parsed JSON
changes the bytes and the signature no longer verifies.

**calls** `hexToBytes`, `importPublicKey`
