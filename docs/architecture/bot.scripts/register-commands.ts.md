<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/register-commands.ts`

@file Upload the command list to Discord.
Guild-scoped by default because guild registration is instant and global
registration lags, which during setup reads as "the bot is broken".
Bulk overwrite (PUT), not create: the array in src/commands/index.ts is the
whole truth, so a command deleted from the code disappears from Discord on
the next run instead of lingering as a slash command with no handler.
DISCORD_APPLICATION_ID=... DISCORD_BOT_TOKEN=... DISCORD_GUILD_ID=... \
npm run register
The token is read from the environment, never from a file in the repo, and
is never printed, including in error paths.

**depends on** [`bot/src/commands/index.ts`](../bot.src.commands/index.ts.md)

<details><summary>Undocumented (1)</summary>

- `die`

</details>
