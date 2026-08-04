<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/spec.ts`

@file `/spec <section>` — which files in this repository cite an Aliro 1.0
section. Pointers only.
The spec text itself (`internal/aliro-1.0.txt`) is gitignored and this bot
never reads it; `SPEC_CITATIONS` is built by scanning the tracked prose
that already cites the spec, not the spec. Answering with anything more
than a file and a line would start reproducing member-confidential text
one paraphrase at a time, which is exactly what this command must not do.

**depends on** [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/spec-index.generated.ts`](../bot.src/spec-index.generated.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

## API

### `export function normaliseSection(raw: string): string | null`
`bot/src/commands/spec.ts:30`

`14`, `11.3.1`, `table 8-3`, `§8.3.3.5.1` all resolve to the stored form.

**called by** `handler`

### `function covers(query: string, cited: string): boolean`
`bot/src/commands/spec.ts:41`

Exact match, or a citation to a subsection of the queried section: asking
for "11" should surface a citation to "11.7.3.4.1". Table numbers have no
such hierarchy and only match exactly.

**called by** `handler`

<details><summary>Undocumented (1)</summary>

- `handler`

</details>
