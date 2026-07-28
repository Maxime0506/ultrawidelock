import { expect, test } from "bun:test"
import { findMatches, matchSummary, splitByMatch, stepMatch } from "../src/search"

// Shaped like the traffic this actually runs against: brackets, colons, hex and
// a question mark, all of which are regular-expression metacharacters.
const log = [
  "[01:43:59.275,146] <inf> chip: [EM]<<< E:7832i S:37909 M:68853588",
  "] (S) Msg TX from 0000000000000003 to 3:000000000001B669 [4B10]",
  "uart:~$ aliro status",
  "[01:43:59.528,076] <inf> chip: [IM]Received status response, status is 0x00"
]

test("matches literally, so log punctuation is not read as a pattern", () => {
  // A regex engine would treat these as a character class, a group and a
  // wildcard. Searching a firmware log would be a syntax minefield.
  expect(findMatches(log, "[EM]")).toEqual([0])
  expect(findMatches(log, "(S)")).toEqual([1])
  expect(findMatches(log, "0x00")).toEqual([3])
  expect(findMatches(log, ".*")).toEqual([])
})

test("matching ignores case and an empty query matches nothing", () => {
  expect(findMatches(log, "STATUS")).toEqual([2, 3])
  expect(findMatches(log, "status")).toEqual([2, 3])
  // Not "every line": an empty box must not claim the whole scrollback is a hit.
  expect(findMatches(log, "")).toEqual([])
  expect(findMatches([], "anything")).toEqual([])
})

test("splitting a line keeps every character exactly once", () => {
  for (const line of log) {
    for (const query of ["chip", "0", "", "zzz", ":"]) {
      const segments = splitByMatch(line, query)
      expect(segments.map(({ text }) => text).join("")).toBe(line)
    }
  }
})

test("splitting flags every occurrence, including repeats on one line", () => {
  const segments = splitByMatch("aliro aliro aliro", "aliro")
  expect(segments.filter(({ hit }) => hit)).toHaveLength(3)
  expect(segments.map(({ text }) => text).join("")).toBe("aliro aliro aliro")

  // The highlight preserves the line's own casing rather than the query's.
  const cased = splitByMatch("Status is OK", "status")
  expect(cased.find(({ hit }) => hit)?.text).toBe("Status")

  // Nothing to highlight is one plain run, so the caller has no special case.
  expect(splitByMatch("plain", "zzz")).toEqual([{ text: "plain", hit: false }])
  expect(splitByMatch("", "a")).toEqual([{ text: "", hit: false }])
})

test("stepping wraps at both ends and survives an empty match list", () => {
  expect(stepMatch(3, 0, 1)).toBe(1)
  expect(stepMatch(3, 2, 1)).toBe(0)
  // Backwards off the front lands on the last hit, not on -1.
  expect(stepMatch(3, 0, -1)).toBe(2)
  expect(stepMatch(0, 0, 1)).toBe(0)
  expect(stepMatch(0, 0, -1)).toBe(0)
})

test("the counter is one-based and says so when there is nothing", () => {
  expect(matchSummary(17, 2)).toBe("3/17")
  expect(matchSummary(1, 0)).toBe("1/1")
  expect(matchSummary(0, 0)).toBe("no matches")
})
