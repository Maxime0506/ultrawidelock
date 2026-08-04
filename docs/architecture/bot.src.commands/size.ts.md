<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/size.ts`

@file `/size` — the current recorded CDK size baseline.
Reads `firmware/size-baseline.json` directly (`src/size-baseline.ts`), the
same file `make cdk-size-check` compares a build against and
`make cdk-size-baseline` rewrites. This is a snapshot from the last commit
that updated it, not a live measurement: nothing here can build firmware,
so a stale answer is possible if the record has not been refreshed since a
change moved the numbers. The commit the baseline was recorded at is always
printed so a reader can judge that for themselves.

**depends on** [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/size-baseline.ts`](../bot.src/size-baseline.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

<details><summary>Undocumented (2)</summary>

- `fmt`
- `handler`

</details>
