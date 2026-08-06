<!-- generated documentation — edit the source, not this file -->
# `scripts/docs-sync.sh`

docs-sync.sh — put the generated artifacts back in step after a merge.
The committed docs are generated, so when a merge brings in someone else's
regeneration they conflict on their derived lines: a subsystem count, a
coverage percentage, a table row. Both sides are right about their own tree
and both are wrong about the merge, so no resolution is a merge. The only
correct output is a fresh generation, which is what this does.
The order is the whole point, because each step invalidates the next:
1. take our side of any conflicted generated file, so the tree parses again
2. drop the parse cache, which otherwise replays pre-merge line numbers
that look plausible and are wrong
3. regenerate docs/, which MOVES line numbers inside docs/ARCHITECTURE.md
Run it through `make sync`.
SYNC_NO_VERIFY=1 stops before the sweep, for when you have another reason to
run it yourself.
