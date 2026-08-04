<!-- generated documentation — edit the source, not this file -->
# `bot/src/rigs.ts`

@file Every statement this Worker runs against D1.
Nothing outside this file writes SQL, and nothing in this file builds SQL
by concatenation. Each query is a constant with bound parameters, so a
value arriving from a Discord field cannot become syntax.

**used by** [`bot/src/commands/context.ts`](../bot.src.commands/context.ts.md), [`bot/src/commands/forget.ts`](../bot.src.commands/forget.ts.md), [`bot/src/commands/help-me.ts`](../bot.src.commands/help-me.ts.md), [`bot/src/commands/ihave.ts`](../bot.src.commands/ihave.ts.md), [`bot/src/commands/matrix.ts`](../bot.src.commands/matrix.ts.md), [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/who-has.ts`](../bot.src.commands/who-has.ts.md), [`bot/src/db.ts`](db.ts.md), [`bot/src/matrix.ts`](matrix.ts.md), [`bot/src/render.ts`](render.ts.md), [`bot/src/roleConnection.ts`](roleConnection.ts.md)

## API

### `export class RegistryUnavailable extends Error`
`bot/src/rigs.ts:25`

Thrown when the D1 binding is missing or a statement fails, so callers
can say "the registry is down" rather than "something went wrong".

### `export async function upsertRig(db: D1Database | undefined, row: Omit<RigRow, "updated_at" | "probe_serial"> & { probe_serial?: string | null }): Promise<void>`
`bot/src/rigs.ts:66`

Record what one contributor has for one board. Re-running it for the same
board replaces that entry rather than accumulating rows.

**called by** `modalHandler`  ·  **calls** `need`, `run`

### `export async function forgetRig(db: D1Database | undefined, userId: string, board?: string): Promise<number>`
`bot/src/rigs.ts:95`

Hard delete. `board` omitted deletes every board for that user. Returns
how many rows went, so the confirmation can be specific.

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function entriesForUser(db: D1Database | undefined, userId: string): Promise<RigRow[]>`
`bot/src/rigs.ts:116`

Every board one person has registered.

**called by** `computeRegistryMetadata`, `handler`, `onModalSubmit`  ·  **calls** `need`, `run`

### `export async function whoHas(db: D1Database | undefined, filter: { board?: string; iosVersion?: string }): Promise<RigRow[]>`
`bot/src/rigs.ts:133`

Owners matching board and/or iOS version, most-recently-updated first.
At least one filter is required by the caller (src/commands/who-has.ts);
this function itself just runs whichever WHERE clause the filters imply.

**called by** `handler`, `handler`  ·  **calls** `need`, `run`

### `export async function matrixCounts(db: D1Database | undefined): Promise<MatrixCount[]>`
`bot/src/rigs.ts:179`

Owner counts for every (board, ios_version) pair that has at least one
owner. A pair absent from the result has zero owners — src/matrix.ts
fills that in as the "nobody owns this" glyph rather than omitting the
cell, since an empty cell is the entire point of the matrix.

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function countForUser(db: D1Database | undefined, userId: string): Promise<number>`
`bot/src/rigs.ts:189`

Used by tests to prove `/forget` leaves no row, and by nothing else.

**calls** `need`, `run`

<details><summary>Undocumented (3)</summary>

- `RegistryUnavailable.constructor`
- `need`
- `run`

</details>
