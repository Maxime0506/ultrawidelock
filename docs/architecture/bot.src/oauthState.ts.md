<!-- generated documentation — edit the source, not this file -->
# `bot/src/oauthState.ts`

@file The CSRF/session-correlation state that chains the two OAuth legs
("Discord authorize" then "GitHub authorize") into one flow, and stops a
forged callback from attaching a GitHub identity to the wrong Discord
user. Every transition is a single guarded statement (same atomic
first-writer-wins shape as `claim()` in testRequests.ts), not a
read-then-write, so a replayed or duplicated callback cannot advance a
state twice.

**used by** [`bot/src/linkedRoles.ts`](linkedRoles.ts.md), [`bot/src/scheduled.ts`](scheduled.ts.md)  ·  **discussed in** [`bot/README.md`](../../../bot/README.md)

## API

### `export async function beginDiscordLeg(db: D1Database | undefined, now: number = Date.now()): Promise<string>`
`bot/src/oauthState.ts:42`

Starts the flow: a fresh random state, staged "discord", not yet tied to
any Discord user (that only becomes known once the Discord leg's
callback runs).

**called by** `startLinkedRole`  ·  **calls** `need`, `run`

### `export async function advanceToGithubLeg(db: D1Database | undefined, state: string, discordUserId: string, now: number = Date.now()): Promise<boolean>`
`bot/src/oauthState.ts:56`

The Discord callback's job: only succeeds once, only within the TTL of
the original `beginDiscordLeg` call, and only from stage "discord" —
a state cannot be advanced twice, so a replayed callback is a no-op.

**called by** `handleDiscordCallback`  ·  **calls** `need`, `run`

### `export async function consumeGithubLeg(db: D1Database | undefined, state: string, now: number = Date.now()): Promise<string | null>`
`bot/src/oauthState.ts:84`

The GitHub callback's job: consumes the state (deletes it, so it cannot
be replayed) and hands back which Discord user this GitHub identity
belongs to, or null if the state is unknown, expired, or was never
advanced past the Discord leg.
A plain SELECT then DELETE rather than `DELETE ... RETURNING`: D1's own
docs (developers.cloudflare.com/d1/sql-api/sql-statements/, checked
2026-08-04) do not confirm RETURNING support, and the race this would
close — two callbacks for the exact same unguessable random state,
arriving concurrently — is not worth depending on an unverified SQL
feature for.

**called by** `handleGithubCallback`  ·  **calls** `need`, `run`

<details><summary>Undocumented (4)</summary>

- `OAuthStateUnavailable`
- `OAuthStateUnavailable.constructor`
- `need`
- `run`

</details>
