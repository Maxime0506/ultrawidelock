<!-- generated documentation — edit the source, not this file -->
# `bot/src/modal.ts`

@file Modal building and parsing.
Discord's current modal system wraps each field in a Label (type 18)
component placed directly in `data.components` — no Action Row wrapper,
which is the older, now-deprecated shape a text-only modal used. Verified
against docs.discord.com/developers/interactions/message-components and
.../components/reference on 2026-08-04: Label carries `label` and
`description`; the wrapped component (Text Input type 4, String Select
type 3) carries no label of its own. A submitted modal comes back as a
*flat* array of component values — not nested inside the Label — each
with `custom_id` and either `value` (text input) or `values` (select).
This is a newer, less-travelled part of the API than the rest of this
bot's wire handling. It has not been proven against a live Discord round
trip; `test/modal.test.ts` proves only that this file's own
build/parse pair agrees with itself and with the shapes documented above.

**depends on** [`bot/src/discord.ts`](discord.ts.md)  ·  **used by** [`bot/src/commands/ihave.ts`](../bot.src.commands/ihave.ts.md)

## API

### `export function buildModal(customId: string, title: string, fields: readonly ModalField[]):`
`bot/src/modal.ts:83`

A modal response body (the `data` half of an interaction response with
`type: 9`). `customId` is where per-invocation state that a ModalSubmit
interaction cannot otherwise recover — e.g. the board this /ihave modal
is for — gets smuggled through.

**called by** `handler`  ·  **calls** `buildComponent`

### `export function textFieldValue(components: SubmittedComponent[] | undefined, customId: string): string | undefined`
`bot/src/modal.ts:103`

One submitted text input's value, trimmed, or undefined if absent, empty,
or not the type expected. `components` is the flat array Discord sends on
MODAL_SUBMIT.

**called by** `modalHandler`

### `export function selectFieldValue(components: SubmittedComponent[] | undefined, customId: string): string | undefined`
`bot/src/modal.ts:115`

One submitted select's chosen value (single-select only: the first of
`values`), or undefined if absent or empty.

**called by** `modalHandler`

<details><summary>Undocumented (1)</summary>

- `buildComponent`

</details>
