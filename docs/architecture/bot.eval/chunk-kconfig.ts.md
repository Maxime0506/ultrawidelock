<!-- generated documentation — edit the source, not this file -->
# `bot/eval/chunk-kconfig.ts`

@file A grammar-aware chunker for Kconfig fragments, and the experiment that
says whether it earns its place.
The prediction being tested is that it will NOT fix the config stratum.
`firmware/prj.conf:226` fails today because the question says "serial port"
and the file says "console", "RTT" and "UART" — and the naive 40-line window
already contains that comment. Attaching the comment more precisely does not
add a word the file never uses.
What it should improve is precision: one chunk per CONFIG symbol, carrying its
own comment and its section header and nothing else, means a hit is the fact
rather than a 40-line neighbourhood that happens to contain it. That shows up
in MRR and recall@5 rather than recall@10.
Writing the prediction down first so the measurement can contradict it.

**depends on** [`bot/eval/corpus.ts`](corpus.ts.md), [`bot/eval/golden.ts`](golden.ts.md)  ·  **used by** [`bot/eval/stage1-probe.ts`](stage1-probe.ts.md)  ·  **discussed in** [`bot/eval/README.md`](../../../bot/eval/README.md)

## API

### `export function chunkKconfig(file: string, lines: string[]): Chunk[]`
`bot/eval/chunk-kconfig.ts:34`

One chunk per `CONFIG_*` assignment: the assignment, the unbroken comment
block directly above it, and the most recent section header.
Consecutive assignments with no comment between them are kept together, since
a run like the PSA_WANT_* block is one decision written as eight lines.

**called by** `hybridChunks`

<details><summary>Undocumented (2)</summary>

- `isKconfig`
- `readLines`

</details>
