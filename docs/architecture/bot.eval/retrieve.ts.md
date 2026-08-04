<!-- generated documentation — edit the source, not this file -->
# `bot/eval/retrieve.ts`

@file The two lexical retrievers Stage 0 measures, over one shared tokenizer.
Both baselines take the same term list, so a difference between them is a
difference in the index and not in how the question was read.
The FTS5 index is built twice on purpose. SQLite's unicode61 tokenizer treats
`_` as a separator, which shreds `CONFIG_UART_CONSOLE` into three ordinary
English words and makes the single most common query shape in this repo
un-retrievable. `tokenchars '_'` fixes it. Measuring both quantifies how much
of "lexical search works here" is really "lexical search configured for
identifiers works here".

**depends on** [`bot/eval/corpus.ts`](corpus.ts.md), [`bot/eval/golden.ts`](golden.ts.md)  ·  **used by** [`bot/eval/independent.ts`](independent.ts.md), [`bot/eval/scope.ts`](scope.ts.md), [`bot/eval/stage0.ts`](stage0.ts.md), [`bot/eval/stage1-probe.ts`](stage1-probe.ts.md)

## API

### `export function terms(question: string): string[]`
`bot/eval/retrieve.ts:37`

Query terms, exact tokens preserved.
Order matters only for readability; scoring is by idf, not position.

**called by** `diagnose`, `ftsSearch`, `ripgrepSearch`  ·  **calls** `keep`

### `export function chunkLocator(chunks: Chunk[]): (file: string, line: number) => Chunk | undefined`
`bot/eval/retrieve.ts:111`

Chunk lookup by file, for turning a rg `file:line` into the chunk holding it.

**called by** `diagnose`, `main`, `main`, `main`, `makeScorer`

<details><summary>Undocumented (5)</summary>

- `keep`
- `buildFtsIndex`
- `ftsSearch`
- `ripgrepSearch`
- `corpusFiles`

</details>
