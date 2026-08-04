<!-- generated documentation — edit the source, not this file -->
# `bot/src/api.ts`

@file The two Discord calls that need the bot token.
Creating a forum post and posting into a thread are the only things this bot
does that an interaction token cannot authorise. The token is a Worker
secret, is never logged, and is never interpolated into anything that could
be echoed back.
Channel IDs are checked against a snowflake pattern before they reach a URL.
They come from configuration rather than from a user, but a URL built by
concatenation is worth validating whatever the source.

**depends on** [`bot/src/env.ts`](env.ts.md)  ·  **used by** [`bot/src/commands/build.ts`](../bot.src.commands/build.ts.md), [`bot/src/commands/help-me.ts`](../bot.src.commands/help-me.ts.md)

## API

### `export function forumChannelFor(env: Env, board: string): string | undefined`
`bot/src/api.ts:33`

The forum channel for a board, or undefined.
FORUM_CHANNELS is `BOARD=id,BOARD=id`. An unparseable entry is skipped
rather than throwing: a typo in one board's channel must not take
`/help-me` down for every other board.

**called by** `onModalSubmit`

### `export async function createForumThread(env: Env, channelId: string, name: string, content: string, allowedMentions: AllowedMentions, correlationId: string): Promise<string | null>`
`bot/src/api.ts:78`

Create a forum post. Returns the thread ID, or null if it could not.

**called by** `onModalSubmit`  ·  **calls** `call`

### `export async function postToThread(env: Env, threadId: string, content: string, correlationId: string): Promise<boolean>`
`bot/src/api.ts:102`

Post a follow-on message into a thread. Best effort.

**called by** `onModalSubmit`  ·  **calls** `call`

### `export async function dispatchFirmwareBuilds(env: Env, ref: string, targets: string, correlationId: string): Promise<`
`bot/src/api.ts:136`

Dispatch firmware-builds.yml with a `targets` selection.
`workflow_dispatch` itself returns 204 with no run identifier, so the run
URL is found by asking for the newest workflow_dispatch run afterward. That
is racy against anyone else dispatching the same workflow in the same
second; `findLatestRun` accepts the gap and the caller degrades to a runs
list link when it cannot find a match.
`save_ccache` is always false here. Every per-job ccache entry is keyed on
`github.run_id`, so a normal dispatch always writes a fresh ~300 MB cache
entry — the right default for a maintainer's own run, wrong for a bot
answering a one-off Discord request: ten of those would be 3 GB against the
repository's 10 GB Actions cache LRU budget, pressure that can evict
nrf-workspace or the esp-matter cache, the two entries actually expensive
to rebuild. `workflow_dispatch` inputs are always strings on the wire, so
this is the literal string "false", not the boolean.

**called by** `handler`

### `export async function findLatestRun(env: Env, since: number, correlationId: string): Promise<string>`
`bot/src/api.ts:181`

Poll briefly for the run a dispatch just created. `since` is the time just
before the dispatch call, so a run created earlier cannot be mistaken for
this one.

**called by** `handler`

<details><summary>Undocumented (1)</summary>

- `call`

</details>
