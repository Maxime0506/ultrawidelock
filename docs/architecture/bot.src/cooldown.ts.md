<!-- generated documentation — edit the source, not this file -->
# `bot/src/cooldown.ts`

@file The `/matrix` PNG-rendering cooldown.
One statement, not a read then a write: SQLite serializes writes to one
database, so the WHERE guard on the UPDATE is the atomicity. Two
concurrent `/matrix` calls from the same user landing in the same
millisecond still only let one through, because there is no gap between
reading the old timestamp and writing the new one for a second statement
to land in.

**used by** [`bot/src/commands/matrix.ts`](../bot.src.commands/matrix.ts.md)

## API

### `export async function checkMatrixCooldown(db: D1Database | undefined, userId: string, cooldownMs: number, now: number): Promise<CooldownCheck>`
`bot/src/cooldown.ts:28`

Checks and, if expired, atomically starts a new cooldown window in the
same statement. A missing or unreachable D1 binding fails *open* here
(nobody is rate-limited): rate limiting is a cost control on the PNG
path, not a correctness guarantee, and `matrixCounts()` — called right
after this in commands/matrix.ts — is what actually surfaces "the
registry is not reachable" if D1 is genuinely down.

**called by** `handler`
