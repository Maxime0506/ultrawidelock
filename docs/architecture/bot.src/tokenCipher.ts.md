<!-- generated documentation — edit the source, not this file -->
# `bot/src/tokenCipher.ts`

@file AES-256-GCM for the one thing this bot stores that is a real bearer
credential rather than an opaque Discord user ID: the OAuth access and
refresh tokens Linked Roles needs to push updated metadata later without
re-prompting the user. D1 has no column-level encryption of its own, so
this exists to keep those tokens unreadable from a raw table dump —
`OAUTH_ENCRYPTION_KEY` is a Worker secret, never a D1 value.

**used by** [`bot/src/oauthLinks.ts`](oauthLinks.ts.md)

## API

### `export async function encryptToken(plaintext: string, secretBase64: string): Promise<string>`
`bot/src/tokenCipher.ts:57`

`iv || ciphertext`, both base64-joined as one string, so storage is one
D1 column rather than two. A fresh random IV every call — AES-GCM must
never reuse an IV under the same key.

**called by** `saveDiscordAccessToken`  ·  **calls** `bytesToBase64`, `deriveKey`

### `export async function decryptToken(blob: string, secretBase64: string): Promise<string>`
`bot/src/tokenCipher.ts:70`

Throws TokenCipherError on a wrong key, corrupted data, or a mismatched
auth tag — GCM authenticates the ciphertext, so tampering is detected
rather than silently producing garbage plaintext.

**called by** `decryptedAccessToken`  ·  **calls** `base64ToBytes`, `deriveKey`

<details><summary>Undocumented (5)</summary>

- `TokenCipherError`
- `TokenCipherError.constructor`
- `base64ToBytes`
- `bytesToBase64`
- `deriveKey`

</details>
