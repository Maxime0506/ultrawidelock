<!-- generated documentation — edit the source, not this file -->
# `bot/src/validations.ts`

@file Every statement this Worker runs against `validations`. Same rule
as rigs.ts and testRequests.ts: no SQL built by concatenation, every
value a bound parameter.

**depends on** [`bot/src/matrix.ts`](matrix.ts.md)  ·  **used by** [`bot/src/commands/matrix.ts`](../bot.src.commands/matrix.ts.md), [`bot/src/commands/test-result.ts`](../bot.src.commands/test-result.ts.md), [`bot/src/roleConnection.ts`](roleConnection.ts.md)

## API

### `export class ValidationsUnavailable extends Error`
`bot/src/validations.ts:9`

Thrown when the D1 binding is missing or a statement fails.

### `export async function recordValidation(db: D1Database | undefined, v: NewValidation): Promise<void>`
`bot/src/validations.ts:48`

Records one test event. Never overwrites a prior result for the same
pair — a re-test is a new row, so the history survives even though
`/matrix` only ever reads the latest.

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function countValidationsByTester(db: D1Database | undefined, userId: string): Promise<number>`
`bot/src/validations.ts:74`

How many test results one person has recorded — the Linked Roles
`validated_runs` metadata field.

**called by** `computeRegistryMetadata`  ·  **calls** `need`, `run`

### `export async function latestValidations(db: D1Database | undefined): Promise<ValidationStatus[]>`
`bot/src/validations.ts:86`

The most recent result per (board, ios_version) — what `/matrix` shows
as ✅/❌. A dead-tie on `tested_at` for the same pair (two rows written
at the exact same millisecond) can duplicate a pair in the result; that
is a display redundancy, not a wrong glyph, since every caller reads
this with `.find()` and takes the first match either way.

**called by** `handler`  ·  **calls** `need`, `run`

<details><summary>Undocumented (3)</summary>

- `ValidationsUnavailable.constructor`
- `need`
- `run`

</details>
