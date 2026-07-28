/**
 * Searching the serial scrollback.
 *
 * Pure string work, kept out of app.tsx so it can be tested directly instead of
 * through a rendered terminal. Matching is case-insensitive and literal: a
 * firmware log is full of `[`, `*`, `0x..` and `?`, so treating the query as a
 * regular expression would turn ordinary searches into syntax errors.
 */

/** One run of a line, flagged when it is part of a match. */
export type Segment = { text: string; hit: boolean }

/** Line indices containing `query`, in order. Empty for an empty query. */
export function findMatches(lines: string[], query: string): number[] {
  if (query === "") return []
  const needle = query.toLowerCase()
  const hits: number[] = []
  for (let index = 0; index < lines.length; index++) {
    if (lines[index].toLowerCase().includes(needle)) hits.push(index)
  }
  return hits
}

/**
 * Split a line into alternating plain and matching runs.
 *
 * Returns a single unflagged segment when there is nothing to highlight, so the
 * caller can render the common case without a special branch.
 */
export function splitByMatch(line: string, query: string): Segment[] {
  if (query === "" || line === "") return [{ text: line, hit: false }]
  const needle = query.toLowerCase()
  const haystack = line.toLowerCase()
  const segments: Segment[] = []
  let cursor = 0
  for (;;) {
    const at = haystack.indexOf(needle, cursor)
    if (at < 0) break
    if (at > cursor) segments.push({ text: line.slice(cursor, at), hit: false })
    segments.push({ text: line.slice(at, at + query.length), hit: true })
    cursor = at + query.length
  }
  if (segments.length === 0) return [{ text: line, hit: false }]
  if (cursor < line.length) segments.push({ text: line.slice(cursor), hit: false })
  return segments
}

/**
 * Step through matches, wrapping at both ends.
 *
 * Wrapping is the behaviour every editor's find has, and without it the last
 * match looks like a dead end on a scrollback that is still growing.
 */
export function stepMatch(count: number, current: number, delta: number): number {
  if (count <= 0) return 0
  return (((current + delta) % count) + count) % count
}

/** The `3/17` counter for the search rule, or a miss that says so. */
export function matchSummary(count: number, current: number): string {
  return count === 0 ? "no matches" : `${current + 1}/${count}`
}
