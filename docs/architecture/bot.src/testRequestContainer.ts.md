<!-- generated documentation — edit the source, not this file -->
# `bot/src/testRequestContainer.ts`

@file The `/test-request` status card, and the one-time ping that goes
with it — kept as two separate messages rather than one, so the ping
(disposable, mentions-bearing, ordinary `content`) never has to be
reconstructed when the card (persistent, Components V2, no `content`
allowed at all per the platform table this bot's spec cites) is edited in
place later.
Component shapes below are per docs.discord.com/developers/components
(checked 2026-08-04): a Section (type 9)'s `accessory` is the documented
way a Components V2 message carries a Button next to text, not an Action
Row inside a Container — the docs show no Container -> Action Row nesting
at all, so that path was not used here. This has not been proven against
a live Discord render, the same caveat as this bot's modal wire format.

**depends on** [`bot/src/boards.ts`](boards.ts.md), [`bot/src/discord.ts`](discord.ts.md)  ·  **used by** [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/test-result.ts`](../bot.src.commands/test-result.ts.md), [`bot/src/scheduled.ts`](scheduled.ts.md)

## API

### `export function requestIdFromCustomId(customId: string): string | null`
`bot/src/testRequestContainer.ts:34`

`test-accept:<uuid>` -> the request id, or null if this is not one of
this bot's Accept buttons.

**called by** `componentHandler`

### `export function buildTestRequestMessage(input: ContainerInput):`
`bot/src/testRequestContainer.ts:97`

The Components V2 message body for the status card: a Container whose
accent color and Accept button state track `status`. Same shape whether
this is the first post or a later in-place edit.

**called by** `componentHandler`, `handler`, `handler`  ·  **calls** `accentColor`, `accessoryButton`, `boardLabel`, `statusLine`

### `export function buildAwakePing(candidateIds: readonly string[]):`
`bot/src/testRequestContainer.ts:128`

The disposable ping that accompanies the first post: ordinary `content`
(not Components V2, so it can carry mentions at all), with an explicit
`allowed_mentions.users` allow-list rather than relying on the default
parse rules — the same "never let user text create a ping" posture as
every other message this bot sends, just inverted to explicitly *permit*
exactly the candidate IDs this Worker itself looked up, not whatever a
free-text field happened to contain.

**called by** `handler`

<details><summary>Undocumented (5)</summary>

- `acceptCustomId` — tested: :accept custom id and request id from custom id round-trip@l22; :accept: first click claims, starts a thread, and edits the card in place to claimed@l82; :accept: the losing click gets a private 'already accepted' note and never touches the card@l123
- `statusLine`
- `accentColor`
- `accessoryButton`
- `buildEscalationPing` — tested: :build awake ping and build escalation ping carry only the given i ds in the mention allow-list, never a bare parse-all@l96

</details>
