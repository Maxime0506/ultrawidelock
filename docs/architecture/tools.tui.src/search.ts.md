<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/search.ts`

Searching the serial scrollback.
Pure string work, kept out of app.tsx so it can be tested directly instead of
through a rendered terminal. Matching is case-insensitive and literal: a
firmware log is full of `[`, `*`, `0x..` and `?`, so treating the query as a
regular expression would turn ordinary searches into syntax errors.

**used by** [`tools/tui/src/app.tsx`](app.tsx.md)

## API

### `export function findMatches(lines: string[], query: string): number[]`
`tools/tui/src/search.ts:14`

Line indices containing `query`, in order. Empty for an empty query.

**called by** `App`, `SerialTerminal`

### `export function splitByMatch(line: string, query: string): Segment[]`
`tools/tui/src/search.ts:30`

Split a line into alternating plain and matching runs.
Returns a single unflagged segment when there is nothing to highlight, so the
caller can render the common case without a special branch.

**called by** `SerialTerminal`

### `export function stepMatch(count: number, current: number, delta: number): number`
`tools/tui/src/search.ts:54`

Step through matches, wrapping at both ends.
Wrapping is the behaviour every editor's find has, and without it the last
match looks like a dead end on a scrollback that is still growing.

**called by** `stepSearch`

### `export function matchSummary(count: number, current: number): string`
`tools/tui/src/search.ts:60`

The `3/17` counter for the search rule, or a miss that says so.

**called by** `App`, `status`
