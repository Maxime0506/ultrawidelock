<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/build.ts`

@file `/build <target>` — dispatch firmware-builds.yml.
The heaviest thing this bot can ask CI for: up to six jobs, the NCS and
ESP-IDF toolchains, tens of minutes. Deferred unconditionally, rate limited
harder than anything else here, and idempotent on the interaction ID so a
retried delivery cannot dispatch twice.

**depends on** [`bot/src/api.ts`](../bot.src/api.ts.md), [`bot/src/build-targets.ts`](../bot.src/build-targets.ts.md), [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/db.ts`](../bot.src/db.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/followup.ts`](../bot.src/followup.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

<details><summary>Undocumented (2)</summary>

- `formatRemaining`
- `handler`

</details>
