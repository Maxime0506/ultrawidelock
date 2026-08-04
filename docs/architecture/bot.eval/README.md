<!-- generated documentation — edit the source, not this file -->
# `bot/eval/`

| subsystem | about |
|---|---|
| [`bot/eval/chunk-kconfig.ts`](chunk-kconfig.ts.md) | @file A grammar-aware chunker for Kconfig fragments, and the experiment that |
| [`bot/eval/corpus.ts`](corpus.ts.md) | @file What gets indexed, and how it is cut up for the Stage 0 baseline. |
| [`bot/eval/deepwiki.ts`](deepwiki.ts.md) | @file Baseline A: DeepWiki, scored against the same golden set. |
| [`bot/eval/expand.ts`](expand.ts.md) | @file The vocabulary alias table, and query expansion over it. |
| [`bot/eval/golden.ts`](golden.ts.md) | @file The golden set, and the gate that keeps its labels honest. |
| [`bot/eval/headers.ts`](headers.ts.md) | @file §7.2 tier 1: a deterministic keyword header on every chunk. |
| [`bot/eval/independent.ts`](independent.ts.md) | @file Validate and score a golden set this session did not write. |
| [`bot/eval/retrieve.ts`](retrieve.ts.md) | @file The two lexical retrievers Stage 0 measures, over one shared tokenizer. |
| [`bot/eval/scope.ts`](scope.ts.md) |  |
| [`bot/eval/score-deepwiki.ts`](score-deepwiki.ts.md) | @file Re-score the cached DeepWiki answers, with a metric that is fair to prose. |
| [`bot/eval/stage0.ts`](stage0.ts.md) | @file Stage 0 baseline D: the lexical floor, measured. |
| [`bot/eval/stage1-probe.ts`](stage1-probe.ts.md) | @file Which candidate fix actually moves the config stratum? |
