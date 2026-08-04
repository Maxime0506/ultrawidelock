<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/help-me.ts`

@file `/help-me` — collect the context once, match it, escalate honestly.
The whole point of this bot. Support here costs three or four round trips
before anyone knows which board, which image, and what the console said, so
this asks for all of it in one form and posts a thread that already has the
answer or already says there isn't one.
Two interactions, not one. Discord will not let a command defer and then
open a modal, so board and image are command options (enumerated, validated
by Discord) and the free text is collected by the modal that the command
returns. The selections ride through on the modal's custom_id.
The matcher never speculates. No match is reported as no match and pings the
maintainer, because "I don't recognise this" is a useful thing to tell
somebody and a plausible guess is not.

**depends on** [`bot/src/api.ts`](../bot.src/api.ts.md), [`bot/src/boards.ts`](../bot.src/boards.ts.md), [`bot/src/citations.ts`](../bot.src/citations.ts.md), [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/db.ts`](../bot.src/db.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/followup.ts`](../bot.src/followup.ts.md), [`bot/src/images.ts`](../bot.src/images.ts.md), [`bot/src/rigs.ts`](../bot.src/rigs.ts.md), [`bot/src/signatures.ts`](../bot.src/signatures.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

## API

### `export function parseModalId(customId: string): Selections | null`
`bot/src/commands/help-me.ts:129`

Parse what the modal carried through, rejecting anything not on the lists.

**called by** `onModalSubmit`  ·  **calls** `isKnownBoard`, `isKnownImage`

### `export function threadName(board: string, expected: string, actual: string): string`
`bot/src/commands/help-me.ts:241`

Discord caps a thread name at 100 characters and newlines read badly in a
channel list.

**called by** `onModalSubmit`  ·  **calls** `boardLabel`

### `export function consoleBlock(text: string): string`
`bot/src/commands/help-me.ts:247`

The console paste, fenced, with the truncation stated rather than implied.

**called by** `onModalSubmit`

### `export function contextBlock(input: BlockInput): string`
`bot/src/commands/help-me.ts:281`

The context block.
Says what is known, what matched and why, and what is still unknown. The
last part matters: a report that looks complete but silently omits the NCS
pin costs the round trip this bot exists to remove.

**called by** `onModalSubmit`  ·  **calls** `boardLabel`, `imageLabel`, `matchesSection`

### `function matchesSection(matches: BlockInput["matches"], budget: number): string`
`bot/src/commands/help-me.ts:318`

The match list, trimmed to `budget` characters, saying what it dropped.

**called by** `contextBlock`  ·  **calls** `formatCitations`

<details><summary>Undocumented (3)</summary>

- `optionBoolean`
- `handler`
- `onModalSubmit`

</details>
