<!-- generated documentation — edit the source, not this file -->
# `bot/src/build-targets.ts`

@file The `targets` choices `firmware-builds.yml` actually accepts.
Hand-copied from `.github/workflows/firmware-builds.yml`'s `workflow_dispatch`
input rather than parsed at request time, because a Worker has no access to
that file. `test/build-targets.test.ts` re-parses the live workflow and
asserts this list matches it exactly, so an added or renamed target fails
the build instead of `/build` silently offering a stale choice.

**used by** [`bot/src/commands/build.ts`](../bot.src.commands/build.ts.md)

<details><summary>Undocumented (1)</summary>

- `isKnownTarget`

</details>
