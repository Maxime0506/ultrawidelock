<!-- generated documentation — edit the source, not this file -->
# `bot/src/discordRest.ts`

@file The handful of Discord REST calls that need a bot token rather than
an interaction token: posting to a fixed channel regardless of where a
command was invoked, starting a thread, and editing a message outside any
live interaction (the scheduled escalation sweep). Everything else in this
bot goes through the interaction's own token — see followup.ts — because
that requires no secret beyond what Discord itself hands the Worker per
request.
Every function here fails soft: log and return null/false rather than
throw, so a REST hiccup degrades one step of a request-routing flow
instead of losing the D1 state already committed around it.

**used by** [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/test-result.ts`](../bot.src.commands/test-result.ts.md), [`bot/src/scheduled.ts`](scheduled.ts.md)

## API

### `export async function postMessage(botToken: string, correlationId: string, channelId: string, body: Record<string, unknown>): Promise<PostedMessage | null>`
`bot/src/discordRest.ts:51`

Posts a message to a channel by ID, independent of any interaction —
what `/test-request` uses to land its Container in the fixed queue
channel rather than wherever the maintainer ran the command.

**called by** `handler`, `runEscalationSweep`  ·  **calls** `call`

### `export async function editMessage(botToken: string, correlationId: string, channelId: string, messageId: string, body: Record<string, unknown>): Promise<boolean>`
`bot/src/discordRest.ts:65`

Edits a channel message by ID outside any live interaction token — the
scheduled escalation sweep is the only caller, since an accept-button
click can (and should) instead edit through its own interaction token
via followup.ts's editOriginalComponents.

**called by** `handler`  ·  **calls** `call`

### `export async function startThreadFromMessage(botToken: string, correlationId: string, channelId: string, messageId: string, name: string): Promise<`
`bot/src/discordRest.ts:81`

Starts a thread from an existing message. Only GUILD_TEXT / GUILD_ANNOUNCEMENT
channels support this — the test-queue channel is assumed to be one.

**called by** `componentHandler`  ·  **calls** `call`

<details><summary>Undocumented (1)</summary>

- `call`

</details>
