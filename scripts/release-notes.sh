#!/usr/bin/env bash
#
# release-notes.sh — render the GitHub release body from release/NOTES.md.in.
#
#   scripts/release-notes.sh v0.5.0                     # preview it
#   scripts/release-notes.sh v0.5.0 out/SHA256SUMS.txt  # what CI publishes
#
# Placeholders: @TAG@ @REPO@ @PAGES@ @CHANGELOG@ @SUMS@
# Env: REPO=owner/name (default openaliro/openaliro)
#
# These notes are also the release email: GitHub renders them into the
# notification it sends watchers, so the checksums stay inside a <details> and
# nothing load-bearing sits below the fold.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-}"
SUMS_FILE="${2:-}"
REPO="${REPO:-openaliro/openaliro}"

if [ -z "$TAG" ]; then
	echo "usage: scripts/release-notes.sh <tag> [sums-file]" >&2
	exit 2
fi

TEMPLATE="$ROOT/release/NOTES.md.in"
[ -f "$TEMPLATE" ] || {
	echo "release-notes: template not found: $TEMPLATE" >&2
	exit 1
}

OWNER="${REPO%%/*}"
NAME="${REPO#*/}"
PAGES="https://$OWNER.github.io/$NAME/flash/"

# ---- this version's changelog section ----------------------------------------
# Matched on the bare version, so `## [0.5.0]` and `## [0.5.0] - 2026-08-05` both
# hit. Falls back to Unreleased: a tag cut before the section was renamed should
# still say what changed. Finding nothing is a valid answer.
changelog=""
CL="$ROOT/CHANGELOG.md"
if [ -f "$CL" ]; then
	version="${TAG#v}"
	section="$(awk -v v="$version" '
		/^## \[/ {
			if (found) exit
			line = $0
			sub(/^## \[/, "", line)
			sub(/\].*$/, "", line)
			if (line == v) { found = 1; print "## What changed"; next }
		}
		found { print }
	' "$CL")"
	if [ -z "$section" ]; then
		section="$(awk '
			/^## \[/ {
				if (found) exit
				if ($0 ~ /^## \[Unreleased\]/) { found = 1; print "## What changed"; next }
			}
			found { print }
		' "$CL")"
	fi
	# Trim trailing blank lines so the template controls the spacing.
	changelog="$(printf '%s\n' "$section" | sed -e :a -e '/^\n*$/{$d;N;};/\n$/ba')"
fi

# ---- checksums ---------------------------------------------------------------
if [ -n "$SUMS_FILE" ] && [ -f "$SUMS_FILE" ]; then
	sums="$(cat "$SUMS_FILE")"
else
	sums="see the SHA256SUMS.txt asset below"
fi

# ---- render ------------------------------------------------------------------
# Bash string replacement, not sed: the changelog and checksum blocks carry
# slashes, brackets and backticks that must never act as pattern syntax.
out="$(cat "$TEMPLATE")"
out="${out//@TAG@/$TAG}"
out="${out//@REPO@/$REPO}"
out="${out//@PAGES@/$PAGES}"
out="${out//@CHANGELOG@/$changelog}"
out="${out//@SUMS@/$sums}"

if printf '%s' "$out" | grep -q '@[A-Z_]*@'; then
	echo "release-notes: a placeholder was left unsubstituted:" >&2
	printf '%s' "$out" | grep -o '@[A-Z_]*@' | sort -u >&2
	exit 1
fi

printf '%s\n' "$out"
