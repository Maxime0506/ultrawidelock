<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/build-spec-index.ts`

@file Regenerate src/spec-index.generated.ts from the live docs/ tree.
npm run spec-index
Run this after editing anything in docs/ that cites the Aliro 1.0
specification. `npm run drift` (via spec-index.test.ts) fails the build if
the committed file falls out of sync with a fresh scan, the same way
citations.ts is checked against the lines it cites.

**depends on** [`bot/scripts/spec-scan.ts`](spec-scan.ts.md)
