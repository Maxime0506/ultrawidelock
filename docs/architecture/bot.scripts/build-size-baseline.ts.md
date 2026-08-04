<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/build-size-baseline.ts`

@file Regenerate src/size-baseline.generated.ts from firmware/size-baseline.json.
npm run size-baseline
Importing the full baseline file directly was tried first and rejected: it
carries a per-symbol breakdown for every recorded config (3,200+ lines) to
answer a question that needs six numbers, and bundling all of it nearly
tripled the Worker (62 KiB gzip -> 193 KiB) for data `/size` never reads.
This extracts only what `/size` prints, the same way spec-index.ts extracts
only citations out of docs/ instead of bundling the prose. `npm run drift`
via size-baseline.test.ts fails if this file falls out of sync.

**depends on** [`bot/scripts/size-baseline-extract.ts`](size-baseline-extract.ts.md)
