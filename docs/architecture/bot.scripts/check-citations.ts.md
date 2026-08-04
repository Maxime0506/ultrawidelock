<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/check-citations.ts`

@file The drift gate.
npm run drift
Fails when a line cited by src/citations.ts no longer says what the table
claims it says, or when a cited file is not one of the paths that starts the
bot workflow. Both are run by .github/workflows/bot.yml.
Exit 0 clean, 1 on a finding.

**depends on** [`bot/scripts/drift.ts`](drift.ts.md), [`bot/src/citations.ts`](../bot.src/citations.ts.md)  ·  **discussed in** [`bot/README.md`](../../../bot/README.md)

<details><summary>Undocumented (1)</summary>

- `readLines`

</details>
