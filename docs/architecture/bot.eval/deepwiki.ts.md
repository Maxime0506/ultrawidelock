<!-- generated documentation — edit the source, not this file -->
# `bot/eval/deepwiki.ts`

@file Baseline A: DeepWiki, scored against the same golden set.
DeepWiki answers in prose rather than returning chunks, so recall@k has no
meaning here and pretending otherwise would make the two baselines look
comparable when they are not. Three things are measured instead:
fact  — the answer contains the gold `expect` text itself
file  — the answer names the file the fact lives in
cite  — the answer gives a `path:line` that RESOLVES to the gold line at
the pinned SHA, which is Amendment 2's third weighted criterion
`fact` is strict and understates a prose answer that paraphrases a doc
comment correctly, so `file` is reported beside it rather than instead of it.
For a config or identifier anchor, where the gold text is a literal token a
correct answer has to print, `fact` is the honest measure.
Responses are cached so scoring can be re-run without re-querying a free
service.

**depends on** [`bot/eval/golden.ts`](golden.ts.md)

## API

### `async function ask(question: string): Promise<Answer>`
`bot/eval/deepwiki.ts:42`

One `tools/call`, over the same streamable-HTTP endpoint the MCP client uses.

**called by** `main`

### `function citesGoldLine(answer: string, file: string, goldLines: number[]): boolean`
`bot/eval/deepwiki.ts:76`

Every `path:line` in the answer that actually resolves to `expect` at HEAD.

**called by** `score`

<details><summary>Undocumented (6)</summary>

- `loadCache`
- `norm`
- `score`
- `hits`
- `mean`
- `main`

</details>
