<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/context.ts`

@file `/context` — the firmware-side `make doctor` the tree does not have.
The HA agent has a `doctor` step (`docs/home-assistant.md:120`); nothing
equivalent exists for the firmware itself. This is not that tool, and it
does not pretend to be: it cannot inspect a contributor's machine from a
Cloudflare Worker. What it can do is print the one fact this repository
pins (the NCS version) next to what a report is missing, as one block
ready to paste, so asking for it is not a fourth round trip.

**depends on** [`bot/src/boards.ts`](../bot.src/boards.ts.md), [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/followup.ts`](../bot.src/followup.ts.md), [`bot/src/rigs.ts`](../bot.src/rigs.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

<details><summary>Undocumented (2)</summary>

- `handler`
- `block` — tested:  registry unreachable@l82; it:cites the repo's own ncs pin@l73; it:lists registered hardware when there is some@l78; it:never claims to know the host os, ncs version or commit@l66

</details>
