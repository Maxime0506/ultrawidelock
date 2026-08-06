#!/usr/bin/env bash
# docs-sync.sh — put the generated artifacts back in step after a merge.
#
# The committed docs are generated, so when a merge brings in someone else's
# regeneration they conflict on their derived lines: a subsystem count, a
# coverage percentage, a table row. Both sides are right about their own tree
# and both are wrong about the merge, so no resolution is a merge. The only
# correct output is a fresh generation, which is what this does.
#
# The order is the whole point, because each step invalidates the next:
#
#   1. take our side of any conflicted generated file, so the tree parses again
#   2. drop the parse cache, which otherwise replays pre-merge line numbers
#      that look plausible and are wrong
#   3. regenerate docs/, which MOVES line numbers inside docs/ARCHITECTURE.md
#
# Run it through `make sync`.
#
# SYNC_NO_VERIFY=1 stops before the sweep, for when you have another reason to
# run it yourself.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> conflicted generated files"
took_ours=0
while IFS= read -r f; do
	[ -n "$f" ] || continue
	git checkout --ours -- "$f"
	git add -- "$f"
	echo "    took ours, to be regenerated: $f"
	took_ours=$((took_ours + 1))
done < <(git diff --name-only --diff-filter=U -- docs/ 2>/dev/null || true)
[ "$took_ours" -eq 0 ] && echo "    none"

# Gitignored and local-only, so this costs a reindex and never a lost artifact.
echo "==> parse cache"
if [ -f .documate/graph.db ]; then
	rm -f .documate/graph.db
	echo "    dropped .documate/graph.db"
else
	echo "    already absent"
fi

echo "==> docs"
make --no-print-directory docs

echo "==> staging"
git add -- docs/

if [ "${SYNC_NO_VERIFY:-0}" = "1" ]; then
	echo
	echo "generated tree is back in step; the sweep was skipped (SYNC_NO_VERIFY=1)."
	exit 0
fi

echo "==> verify"
make --no-print-directory verify

echo
echo "generated tree is back in step and the sweep is green — commit when ready."
