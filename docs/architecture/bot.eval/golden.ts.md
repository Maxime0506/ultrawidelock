<!-- generated documentation — edit the source, not this file -->
# `bot/eval/golden.ts`

@file The golden set, and the gate that keeps its labels honest.
Gold labels key on an `expect` substring, never on a bare line number. That is
not a style choice: the design brief this eval was written against cited
`mk/cdk.mk:261-262`, and commit 3737673 grew that file by 71 lines and moved
every one of those anchors a uniform +20. A golden set keyed on line numbers
would have silently measured every baseline against the wrong lines.
Same mechanism as scripts/check-citations.ts, one level stricter: an `expect`
that matches more than one line in its file is reported too, because an
ambiguous anchor makes recall look better than it is.

**used by** [`bot/eval/chunk-kconfig.ts`](chunk-kconfig.ts.md), [`bot/eval/corpus.ts`](corpus.ts.md), [`bot/eval/deepwiki.ts`](deepwiki.ts.md), [`bot/eval/independent.ts`](independent.ts.md), [`bot/eval/retrieve.ts`](retrieve.ts.md), [`bot/eval/scope.ts`](scope.ts.md), [`bot/eval/score-deepwiki.ts`](score-deepwiki.ts.md), [`bot/eval/stage0.ts`](stage0.ts.md), [`bot/eval/stage1-probe.ts`](stage1-probe.ts.md)

## API

### `export function resolveAnchor(anchor: GoldAnchor): ResolvedAnchor`
`bot/eval/golden.ts:59`

Every 1-indexed line in `file` containing `expect`.

**called by** `main`, `validate`  ·  **calls** `lines`

### `export function headSha(): string`
`bot/eval/golden.ts:77`

The SHA the whole eval is pinned to, for the report header. Shelled out
rather than read from .git/HEAD, because in a git worktree .git is a file.

**called by** `main`, `main`, `main`

<details><summary>Undocumented (3)</summary>

- `lines`
- `loadGolden`
- `main`

</details>
