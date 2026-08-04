<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/index.ts`

@file The command table.
Registration and dispatch read the same list, so a command cannot be
registered with Discord without a handler behind it, or gain a handler
nobody can invoke.
The modal and component routes are derived from the modules too, rather than
kept as their own hand-written maps here: a table in this file lets a command
ship a handler that nothing routes to, and the two drift apart silently.

**depends on** [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/commands/build.ts`](build.ts.md), [`bot/src/commands/context.ts`](context.ts.md), [`bot/src/commands/decode-devid.ts`](decode-devid.ts.md), [`bot/src/commands/forget.ts`](forget.ts.md), [`bot/src/commands/help-me.ts`](help-me.ts.md), [`bot/src/commands/ihave.ts`](ihave.ts.md), [`bot/src/commands/matrix.ts`](matrix.ts.md), [`bot/src/commands/ping.ts`](ping.ts.md), [`bot/src/commands/size.ts`](size.ts.md), [`bot/src/commands/spec.ts`](spec.ts.md), [`bot/src/commands/test-request.ts`](test-request.ts.md), [`bot/src/commands/test-result.ts`](test-result.ts.md), [`bot/src/commands/twin.ts`](twin.ts.md), [`bot/src/commands/verify.ts`](verify.ts.md), [`bot/src/commands/who-has.ts`](who-has.ts.md), [`bot/src/commands/why.ts`](why.ts.md)  ·  **used by** [`bot/scripts/register-commands.ts`](../bot.scripts/register-commands.ts.md), [`bot/src/index.ts`](../bot.src/index.ts.md)

## API

### `function routes(pick: (m: CommandModule) => { prefix?: string; handler?: CommandHandler }):`
`bot/src/commands/index.ts:65`

Longest prefix first, so a future `help-me-extra` cannot be swallowed by
`help-me`.

<details><summary>Undocumented (2)</summary>

- `modalHandlerFor`
- `componentHandlerFor`

</details>
