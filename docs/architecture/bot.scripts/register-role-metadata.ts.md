<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/register-role-metadata.ts`

@file Registers this application's Linked Roles metadata schema (a
separate, one-time-per-change registration from the slash commands in
register-commands.ts — different endpoint, no guild involved, since role
connection metadata is scoped to the application, not a guild).
Run with DISCORD_APPLICATION_ID and DISCORD_BOT_TOKEN in the environment.
Neither belongs in this repository.

**depends on** [`bot/src/roleConnection.ts`](../bot.src/roleConnection.ts.md)
