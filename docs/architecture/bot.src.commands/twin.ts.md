<!-- generated documentation — edit the source, not this file -->
# `bot/src/commands/twin.ts`

@file `/twin approach` and `/twin explain` — the WASM digital twin, run inside this Worker.
`/twin approach` drives the real compiled woz_uwb responder (twin.ts,
web-twin/twin.js + twin.wasm) through a simulated walk-up and reports what
the trust gate actually decided, with a file:line citation. It answers
protocol/crypto-maths questions; it proves nothing about PDoA/AoA, NFC
Express Mode, iOS point-release behaviour, or physical approach unlock —
every reply says so.
`/twin explain <hex>` is NOT implemented. twin_glue.c exports no entry
point that ingests a raw wire frame — every call in it synthesizes frames
from a target distance (twin_mk_prepoll/twin_mk_final_data), and the whole
exchange is CCM*-encrypted against a twin-internal test URSK a real board's
traffic was never encrypted under. Decoding a pasted ranging block from a
bug report would need a new C-side entry point in ccc_shim_rx.c/twin_glue.c
— a firmware change, which this instrumentation task is not allowed to
make (see the parent prompt's Hard Constraint 4). The subcommand is
registered so `/twin explain` names its own gap instead of 404ing, and
says exactly that rather than attempting a decode that cannot work.

**depends on** [`bot/src/citations.ts`](../bot.src/citations.ts.md), [`bot/src/command.ts`](../bot.src/command.ts.md), [`bot/src/discord.ts`](../bot.src/discord.ts.md), [`bot/src/followup.ts`](../bot.src/followup.ts.md), [`bot/src/twin.ts`](../bot.src/twin.ts.md)  ·  **used by** [`bot/src/commands/index.ts`](index.ts.md)

## API

### `function leg(c: { file: string; line: number }, endLine: number): string`
`bot/src/commands/twin.ts:127`

One leg of the exchange as `twin_glue.c:143-149`. The anchor line comes from
the drift-checked table; only the end of the span is written here, since
`Citation` is a single line and a range has nothing to re-read.

**called by** `sequenceDiagram`

<details><summary>Undocumented (10)</summary>

- `subcommand`
- `numberOpt`
- `stringOpt`
- `fmtCm`
- `sequenceDiagram`
- `roundsTable`
- `formatApproach`
- `handleApproach`
- `handleExplain`
- `handler`

</details>
