<!-- generated documentation — edit the source, not this file -->
# `bot/src/command.ts`

@file The shape every command file exports: a Discord command definition
(for registration) and a handler (for dispatch), together so the two
never drift apart.
Separate from commands/index.ts so that followup.ts can take a context
without importing the command table that imports it back.

**depends on** [`bot/src/discord.ts`](discord.ts.md), [`bot/src/env.ts`](env.ts.md)  ·  **used by** [`bot/src/commands/build.ts`](../bot.src.commands/build.ts.md), [`bot/src/commands/context.ts`](../bot.src.commands/context.ts.md), [`bot/src/commands/decode-devid.ts`](../bot.src.commands/decode-devid.ts.md), [`bot/src/commands/forget.ts`](../bot.src.commands/forget.ts.md), [`bot/src/commands/help-me.ts`](../bot.src.commands/help-me.ts.md), [`bot/src/commands/ihave.ts`](../bot.src.commands/ihave.ts.md), [`bot/src/commands/index.ts`](../bot.src.commands/index.ts.md), [`bot/src/commands/matrix.ts`](../bot.src.commands/matrix.ts.md), [`bot/src/commands/ping.ts`](../bot.src.commands/ping.ts.md), [`bot/src/commands/size.ts`](../bot.src.commands/size.ts.md), [`bot/src/commands/spec.ts`](../bot.src.commands/spec.ts.md), [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/test-result.ts`](../bot.src.commands/test-result.ts.md), [`bot/src/commands/twin.ts`](../bot.src.commands/twin.ts.md), [`bot/src/commands/verify.ts`](../bot.src.commands/verify.ts.md), [`bot/src/commands/who-has.ts`](../bot.src.commands/who-has.ts.md), [`bot/src/commands/why.ts`](../bot.src.commands/why.ts.md), [`bot/src/followup.ts`](followup.ts.md)
