<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/drift.ts`

@file The drift gate's logic, with no filesystem in it.
A stale triage table is worse than no triage table, because people trust it.
These checks are what stop src/citations.ts from turning into folklore: if an
edit to mk/cdk.mk moves the line a citation points at, CI fails here rather
than the bot quietly citing the wrong line at somebody for a year.
Kept free of `node:fs` so the tests can drive it with fixtures instead of
rewriting the repository.

**depends on** [`bot/src/citations.ts`](../bot.src/citations.ts.md)  ·  **used by** [`bot/scripts/check-citations.ts`](check-citations.ts.md)

## API

### `export function checkCitations(read: (file: string) => string[] | null, citations: Citation[]): Drift[]`
`bot/scripts/drift.ts:24`

`read` returns the file's lines, or null if it does not exist.

### `export function workflowPathPatterns(yaml: string): string[]`
`bot/scripts/drift.ts:56`

The `paths:` entries of a workflow, flattened across every trigger block.
Deliberately a small scanner rather than a YAML parser: the shape it reads is
four lines of this repository's own workflow, and a dependency that parses
arbitrary YAML is a larger thing to trust than the check is worth.

### `export function choiceInputOptions(yaml: string): string[]`
`bot/scripts/drift.ts:89`

The `options:` list of a `workflow_dispatch` choice input, e.g.
`firmware-builds.yml`'s `targets`. Reads every `- item` line between
`options:` and the first line at or above its own indentation, skipping
comments the same way `workflowPathPatterns` does.

### `export function uncoveredFiles(patterns: string[], files: string[]): string[]`
`bot/scripts/drift.ts:125`

Cited files that no workflow trigger covers.
This is the half that makes the gate real. The bot's job only runs when its
own trigger matches, so a citation into mk/cdk.mk is unchecked unless
mk/cdk.mk is one of the paths that starts the job. Without this, editing the
cited line would be exactly the change that does not run the check.

**calls** `covers`

<details><summary>Undocumented (1)</summary>

- `covers`

</details>
