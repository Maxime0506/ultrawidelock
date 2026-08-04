<!-- generated documentation — edit the source, not this file -->
# `bot/src/oauthLinks.ts`

@file Every statement this Worker runs against `oauth_links`. Same rule
as every other D1 module here: no SQL built by concatenation, every
value a bound parameter. Token encryption itself lives in
tokenCipher.ts — this file only ever handles already-encrypted blobs.

**depends on** [`bot/src/tokenCipher.ts`](tokenCipher.ts.md)  ·  **used by** [`bot/src/linkedRoles.ts`](linkedRoles.ts.md), [`bot/src/scheduled.ts`](scheduled.ts.md)

## API

### `export async function saveDiscordAccessToken(db: D1Database | undefined, discordUserId: string, accessToken: string, encryptionKey: string, now: number = Date.now()): Promise<void>`
`bot/src/oauthLinks.ts:64`

The Discord leg of the flow: parks the encrypted access token where the
GitHub leg can pick it up, leaving any already-linked GitHub identity
untouched (only the Discord columns are in the UPDATE branch).
The access token is the only piece of the grant kept. Discord also returns a
refresh token and an expiry, and both used to be stored; the only thing that
ever read them was a refresh branch guarding against a token expiring between
the two legs, which cannot happen — the legs are one redirect apart and the
grant lasts days. Storing a long-lived refresh token to guard an impossible
case was the worst trade in this file.

**called by** `handleDiscordCallback`  ·  **calls** `encryptToken`, `need`, `run`

### `export async function saveGithubLink(db: D1Database | undefined, discordUserId: string, github: { githubId: number; githubLogin: string }, linkedAt: number): Promise<boolean>`
`bot/src/oauthLinks.ts:83`

The GitHub leg: the row must already exist from `saveDiscordTokens` —
this only ever UPDATEs, never INSERTs, since a GitHub identity with no
Discord tokens behind it is not a state this flow can act on. Returns
whether a row was actually updated.

**called by** `handleGithubCallback`  ·  **calls** `need`, `run`

### `export async function decryptedAccessToken(row: OAuthLinkRow, encryptionKey: string): Promise<string | null>`
`bot/src/oauthLinks.ts:118`

Decrypts the parked access token for a row, or null once it has been
scrubbed. Split out from `getLink` so a caller that only needs to know
*whether* someone is linked (no token material) never has to touch the
cipher at all.

**called by** `pushMetadataNow`  ·  **calls** `decryptToken`

### `export async function scrubLinkSecrets(db: D1Database | undefined, discordUserId: string): Promise<void>`
`bot/src/oauthLinks.ts:141`

Empty every credential-bearing column, leaving who linked and when.
Called once the metadata push succeeds, which is the last moment anything
needs the token or the GitHub login: Discord keeps the pushed metadata on
its own side, and re-running `/linked-role` re-authorises from scratch
rather than reusing what is here. `github_id` stays because it is an opaque
number that identifies nobody on its own and is what a future re-push would
key on; the login, which is a username, does not.

**called by** `pushMetadataNow`  ·  **calls** `need`, `run`

### `export async function purgeAbandonedLinks(db: D1Database | undefined, olderThan: number): Promise<number>`
`bot/src/oauthLinks.ts:159`

Delete rows abandoned between the two OAuth legs.
Somebody who authorises Discord and then closes the tab leaves a live token
behind that no completion path will ever scrub. Without this the "in-flight
only" claim on the table would hold for every user who finishes and for none
who does not, which is the wrong way round: an abandoned flow is exactly the
case where nobody is watching. Returns how many rows went.

**called by** `runAbandonedLinkPurge`  ·  **calls** `need`, `run`

<details><summary>Undocumented (6)</summary>

- `OAuthLinksUnavailable`
- `OAuthLinksUnavailable.constructor`
- `need`
- `run`
- `getLink` — tested: :decrypted access token returns null once scrubbed, rather than throwing@l119; :get link returns null for an unlinked user@l78; :mark metadata pushed sets the timestamp@l65; :purge abandoned links deletes a stranded flow and spares one that finished@l132; :purge abandoned links spares a flow that is still in its window@l153; :re-running save discord access token does not clobber an already-linked git hub identity@l38; :save discord access token stamps linked at and token written at with now@l165; :save discord access token stores the token encrypted (not as plaintext) and get link round-trips them@l20
- `markMetadataPushed` — tested: :mark metadata pushed sets the timestamp@l65; :scrub link secrets empties every credential column and keeps the record of the link@l94

</details>
