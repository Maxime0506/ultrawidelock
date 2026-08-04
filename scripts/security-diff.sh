#!/usr/bin/env bash
#
# security-diff.sh — the structural half of the malicious-change gate.
#
# security/semgrep-malicious.yml asks what a diff SAYS. This asks what a diff DOES to the shape
# of the tree: a binary appearing where only source lives, a file quietly gaining its executable
# bit, a symlink pointing out of the checkout, a submodule nobody discussed, a capture file that
# SECURITY.md says carries the session URSK. None of those are expressible as a source pattern,
# because in every case the payload is opaque to a text scanner — that is the point of using
# them. So they are checked here, against `git diff --raw`, which reports mode and blob type
# whatever the bytes happen to be.
#
#   scripts/security-diff.sh                 # merge-base with origin/main .. HEAD
#   scripts/security-diff.sh <base>          # <base> .. HEAD
#   scripts/security-diff.sh <base> <head>   # explicit range, what CI passes
#
# Exit 0 clean or warnings only, 1 if anything blocking was found, 2 on bad usage.
#
# Two severities, and the split is deliberate. BLOCK is for changes with no legitimate form in
# this repository — checked against the tree as it stands, which has zero symlinks, zero
# gitlinks, five binary files (two in assets/, three fuzz corpus seeds) and thirty executables
# that are every one of them a shell or python script. WARN is for changes that are usually
# fine but are worth a reviewer's eye: a new dependency, a workflow edit, a new remote URL.
# Warnings do not fail the gate, because a gate that cries wolf on a Dependabot bump is a gate
# that gets bypassed, and then the blocking half goes with it.
#
# Env:
#   SECDIFF_MAX_KB=512   size above which an added file is blocking (see BINARY_OK_DIRS)
#   NO_COLOR=1           plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

MAX_KB="${SECDIFF_MAX_KB:-512}"

# ---- glyphs + colour (same vocabulary as scripts/verify.sh) ----------------
if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' YEL=$'\033[33m'
	GRN=$'\033[32m' RESET=$'\033[0m'
	CRS="✗" WRN="!" CHK="✓"
else
	BOLD="" DIM="" RED="" YEL="" GRN="" RESET=""
	CRS="x" WRN="!" CHK="+"
fi

nblock=0
nwarn=0

# Print a blocking issue to stderr in red with the given title and detail, and increment the block
# counter.
block() {
	printf '  %s%s%s %s%s%s\n      %s\n' \
		"$RED" "$CRS" "$RESET" "$BOLD" "$1" "$RESET" "$2"
	nblock=$((nblock + 1))
}
# Print a warning to stderr in yellow with the given title and detail, and increment the warning
# counter.
warn() {
	printf '  %s%s%s %s\n      %s%s%s\n' \
		"$YEL" "$WRN" "$RESET" "$1" "$DIM" "$2" "$RESET"
	nwarn=$((nwarn + 1))
}

# ---- resolve the range ----------------------------------------------------
# Locally the interesting range is "what this branch adds to main", not "the last commit", so
# the default is the merge base. CI passes both ends explicitly because a PR's base branch moves
# under it and `origin/main` on a runner is whatever the checkout depth happened to fetch.
BASE="${1:-}"
HEAD_REF="${2:-HEAD}"

if [ -z "$BASE" ]; then
	for cand in origin/main main origin/master master; do
		if git rev-parse --verify --quiet "$cand" >/dev/null 2>&1; then
			BASE="$(git merge-base "$cand" "$HEAD_REF" 2>/dev/null)" && break
		fi
	done
fi
if [ -z "$BASE" ]; then
	echo "security-diff.sh: cannot resolve a base revision; pass one explicitly" >&2
	exit 2
fi
if ! git rev-parse --verify --quiet "$BASE" >/dev/null 2>&1; then
	echo "security-diff.sh: '$BASE' is not a revision in this repository" >&2
	exit 2
fi

# Working-tree mode. With no explicit head, the interesting question locally is "what would I be
# about to push", and that has to include changes not yet committed — otherwise running this on a
# dirty tree with nothing committed compares HEAD to itself, examines zero files, and prints a
# tick. A gate that reports success having read nothing is the failure this whole file exists to
# catch, so the tree is included unless a caller (CI) names both ends explicitly.
WORKTREE=0
[ "$#" -lt 2 ] && WORKTREE=1
# Unquoted on use, so that in working-tree mode it expands to nothing and `git diff <base>`
# compares base against the tree rather than against a second revision.
DIFF_HEAD="$HEAD_REF"
[ "$WORKTREE" = 1 ] && DIFF_HEAD=""

# ---- allowlists -----------------------------------------------------------
# Directories where a binary is the expected content rather than a surprise. Kept as a prefix
# list rather than a glob so a nested path cannot slip in under a matching leaf name.
#
# bot/src/twin.wasm is named exactly, not a bot/src/* glob: it is the single WASM module
# extracted from web-twin/twin.js's embedded bytes (bot/scripts/twin-wasm-extract.ts, never
# hand-edited), the one way `/twin` can run that firmware inside workerd rather than a browser
# (docs/twin-worker-phase0.md — workerd refuses runtime WASM codegen from bytes). Its size and
# sha256 are pinned in bot/src/twin.lock.json and re-checked every run by
# bot/test/twin-wasm-drift.test.ts, so a swapped or stale blob fails that gate before this one
# would ever need to catch it. A bare bot/src/* entry would let any other surprise binary in
# unreviewed; this does not.
#
# bot/assets/fonts/*.ttf are the two Inter weights satori lays text out with for /matrix's PNG.
# Restricted to *.ttf inside that one directory rather than bot/assets/*, so the folder cannot
# become a general dumping ground. Note this is NOT covered by the assets/* prefix above, which
# is anchored at the repository root.
#
# bot/src/assets.generated.ts is text, not a blob, but trips the size gate at ~4 MB: it is
# `npm run generate-assets` base64-embedding those fonts plus @resvg/resvg-wasm's WASM, so that
# Wrangler and `node --test` receive byte-identical assets without a bundler in between
# (bot/scripts/generate-assets.ts explains why an import rule cannot span both). Generated,
# never hand-edited, and reproducible by re-running that script — which is the review path for
# it, since nobody reads 4 MB of base64.
binary_ok() {
	case "$1" in
	assets/* | tests/host/fuzz/corpus/* | docs/*.png | docs/*.svg | bot/src/twin.wasm) return 0 ;;
	bot/assets/fonts/*.ttf | bot/src/assets.generated.ts) return 0 ;;
	*) return 1 ;;
	esac
}

# A file allowed to carry the executable bit: something with a shebang, in other words a script.
# The tree's thirty executables are 23 *.sh, 4 *.py and 3 extensionless launchers under
# host/presence/. Anything else gaining +x is either a compiled artifact that should not be
# tracked, or a payload waiting for something to run it.
exec_ok() {
	case "$1" in
	*.sh | *.py | host/presence/*) return 0 ;;
	*) return 1 ;;
	esac
}

# Extensions that must never be committed. SECURITY.md states flight-recorder logs embed the
# session URSK, and .gitignore says radio captures carry personal Bluetooth data — so these are
# blocking on the file name alone, before anything tries to read them.
SENSITIVE_RE='\.(log|frc|pcap|pcapng|pklg|btsnoop|pem|key|p12|pfx|jks|keystore|kdbx|ovpn)$'

# ---- the structural pass over the diff ------------------------------------
# --raw gives "<srcmode> <dstmode> <srcsha> <dstsha> <status>\t<path>", which carries the two
# things a text diff throws away: the file mode and whether git considered the blob binary.
# -z is not used: the field split below is on tab, and NUL handling in bash 3.2 is not worth the
# complexity for paths this repo does not have.
if [ "$WORKTREE" = 1 ]; then
	# One rev, not two: `git diff <base>` compares base against the working tree, so staged and
	# unstaged edits are both in scope.
	raw="$(git diff --raw -M "$BASE" 2>/dev/null)"
	untracked="$(git ls-files --others --exclude-standard 2>/dev/null)"
	scope="$(git rev-parse --short "$BASE")..working tree"
else
	raw="$(git diff --raw -M "$BASE" "$HEAD_REF" 2>/dev/null)"
	untracked=""
	scope="$(git rev-parse --short "$BASE")..$(git rev-parse --short "$HEAD_REF")"
fi

nfiles=0
[ -n "$raw" ] && nfiles=$(printf '%s\n' "$raw" | grep -c . )
nuntracked=0
[ -n "$untracked" ] && nuntracked=$(printf '%s\n' "$untracked" | grep -c . )

printf '\n%smalicious-change gate%s  %s%s%s\n' "$BOLD" "$RESET" "$DIM" "$scope" "$RESET"
printf '  %sexamining %d changed + %d untracked file(s)%s\n\n' \
	"$DIM" "$nfiles" "$nuntracked" "$RESET"

if [ -z "$raw" ] && [ -z "$untracked" ]; then
	printf '  %s%s%s nothing to examine: no changes against %s\n\n' \
		"$GRN" "$CHK" "$RESET" "$(git rev-parse --short "$BASE")"
	exit 0
fi

# is_binary PATH SHA — git's own rule, applied directly: a blob is binary if a NUL byte appears
# in its first 8000 bytes. Never a guess from the extension.
#
# Implemented by counting bytes rather than by piping git into grep, and that is not a style
# choice. This script runs under `set -o pipefail`, and `git diff --no-index` exits 1 whenever the
# two inputs differ — which is always, here. Under pipefail the pipeline then reports git's 1
# rather than grep's 0, so the obvious `git diff … | grep -q` form returns "not binary" for every
# file on earth. It read as correct and silently disabled the check.
#
# An all-zero sha means the content exists only in the working tree (an unstaged edit, or an
# untracked file), so the bytes come from disk instead of from a blob.
is_binary() {
	local kept total
	if [ "${2#0000000}" != "$2" ]; then
		total="$(head -c 8000 "$1" 2>/dev/null | wc -c | tr -d ' ')"
		kept="$(head -c 8000 "$1" 2>/dev/null | LC_ALL=C tr -d '\000' | wc -c | tr -d ' ')"
	else
		total="$(git cat-file -p "$2" 2>/dev/null | head -c 8000 | wc -c | tr -d ' ')"
		kept="$(git cat-file -p "$2" 2>/dev/null | head -c 8000 | LC_ALL=C tr -d '\000' \
			| wc -c | tr -d ' ')"
	fi
	[ "${kept:-0}" != "${total:-0}" ]
}

# size_of PATH SHA — same split, for the same reason.
size_of() {
	case "$2" in
	0000000*) wc -c < "$1" 2>/dev/null | tr -d ' ' ;;
	*) git cat-file -s "$2" 2>/dev/null ;;
	esac
}

# inspect PATH DSTMODE STATUS DSTSHA — every structural check, for one file. Factored out so an
# untracked file gets exactly the same treatment as a committed one; when this lived inline, the
# working-tree path would have quietly received a weaker set of checks than CI applies.
inspect() {
	local path="$1" dstmode="$2" status="$3" dstsha="$4" target sz

	case "$dstmode" in
	160000)
		block "submodule added: $path" \
			"A gitlink points at a commit in another repository that nothing in this tree pins or reviews. west.yml is how this project takes external code."
		return
		;;
	120000)
		target="$(git cat-file -p "$dstsha" 2>/dev/null || readlink "$path" 2>/dev/null)"
		block "symlink added: $path -> ${target:-?}" \
			"This repo tracks no symlinks. One pointing outside the checkout turns any build step that follows it into a read of the host filesystem."
		return
		;;
	esac

	if [ "$dstmode" = 100755 ] && ! exec_ok "$path"; then
		block "executable bit set on a non-script: $path" \
			"Every executable in this tree is a shell or python script. A binary that is merely present has to be invoked; one that is +x is one CI step away from running."
	fi

	# Content-shape checks apply to newly added files only: a file already in the tree has
	# been through this gate once, and re-flagging it would fire on every later edit.
	case "$status" in
	A*) ;;
	*) return ;;
	esac

	if printf '%s' "$path" | grep -qiE "$SENSITIVE_RE"; then
		block "sensitive file type added: $path" \
			"SECURITY.md: flight-recorder logs embed the session URSK, and captures carry personal Bluetooth data. Key and certificate stores never belong in history either — history is forever, even after a revert."
	fi

	if is_binary "$path" "$dstsha"; then
		if binary_ok "$path"; then
			warn "binary added in an allowed directory: $path" \
				"assets/ and the fuzz corpus are expected to hold binaries. Confirm this one is what it claims to be."
		else
			block "binary file added: $path" \
				"A blob is opaque to every other gate in this repo: semgrep cannot parse it, review cannot read it. Add it under assets/ or the fuzz corpus, or do not add it."
		fi
	fi

	sz="$(size_of "$path" "$dstsha")"
	sz="${sz:-0}"
	if [ "$sz" -gt $((MAX_KB * 1024)) ] 2>/dev/null && ! binary_ok "$path"; then
		block "oversized file added: $path ($((sz / 1024)) KB > ${MAX_KB} KB)" \
			"Large additions are where payloads hide, because nobody reads to the end of them. Split it, or justify it in the pull request."
	fi
}

while IFS= read -r line; do
	[ -n "$line" ] || continue
	meta="${line%%	*}"
	path="${line#*	}"
	# A rename carries two paths; the destination is the one that exists afterwards.
	case "$path" in *"	"*) path="${path##*	}" ;; esac

	set -- $meta
	# Deletions cannot introduce anything.
	case "$5" in D*) continue ;; esac
	inspect "$path" "$2" "$5" "$4"
done <<EOF
$raw
EOF

# Untracked files. `git diff` cannot see these at all, which is the gap that mattered most: a
# payload dropped into the tree and never staged was invisible to every check above. They are
# treated as additions, because that is what they would be.
while IFS= read -r path; do
	[ -n "$path" ] || continue
	if [ -L "$path" ]; then
		mode=120000
	elif [ -x "$path" ]; then
		mode=100755
	else
		mode=100644
	fi
	inspect "$path" "$mode" "A" "0000000"
done <<EOF
$untracked
EOF

# ---- advisory checks ------------------------------------------------------
# Everything below warns. Each one is a normal thing to do that is also the first step of an
# attack, so the gate's job is to put it in front of a reviewer, not to stop it.
if [ "$WORKTREE" = 1 ]; then
	changed="$(printf '%s\n%s\n' \
		"$(git diff --name-only --diff-filter=ACMR "$BASE" 2>/dev/null)" "$untracked")"
else
	changed="$(git diff --name-only --diff-filter=ACMR "$BASE" "$HEAD_REF" 2>/dev/null)"
fi

wf="$(printf '%s\n' "$changed" | grep -E '^\.github/(workflows|actions)/' || true)"
if [ -n "$wf" ]; then
	warn "CI workflow changed: $(printf '%s' "$wf" | tr '\n' ' ')" \
		"A workflow edit runs with the repository's token. Check the trigger, the permissions block, and that every third-party action is still pinned to a full SHA."
fi

manifests="$(printf '%s\n' "$changed" \
	| grep -E '(package\.json|bun\.lock|pyproject\.toml|requirements[^/]*\.txt|idf_component\.yml|west\.yml)$' || true)"
if [ -n "$manifests" ]; then
	added_deps="$(git diff -U0 "$BASE" $DIFF_HEAD -- $manifests 2>/dev/null \
		| grep -E '^\+' | grep -vE '^\+\+\+' | grep -cE '"[^"]+"\s*:\s*"[^"]*"|^\+\s*[a-zA-Z0-9_.-]+\s*[=<>~^]' || true)"
	warn "dependency manifest changed ($added_deps added line(s)): $(printf '%s' "$manifests" | tr '\n' ' ')" \
		"New dependencies are the most common way hostile code enters a project it was never committed to. Check the package exists, is the one you meant, and is not a typosquat of it."
fi

urls="$(git diff -U0 "$BASE" $DIFF_HEAD -- '*.html' '*.js' '*.ts' '*.py' '*.sh' 2>/dev/null \
	| grep -E '^\+' | grep -vE '^\+\+\+' \
	| grep -oE 'https?://[a-zA-Z0-9._~:/?#@!$&*+,;=%-]+' \
	| grep -vE '^https?://(localhost|127\.0\.0\.1|github\.com/openaliro|(www\.)?w3\.org|schemas?\.|spdx\.org)' \
	| sort -u || true)"
if [ -n "$urls" ]; then
	n="$(printf '%s\n' "$urls" | wc -l | tr -d ' ')"
	warn "$n new remote URL(s) introduced" \
		"$(printf '%s' "$urls" | head -5 | tr '\n' ' ')"
fi

# ---- summary --------------------------------------------------------------
printf '\n'
if [ "$nblock" -gt 0 ]; then
	printf '  %s%s %d blocking, %d advisory%s\n\n' \
		"$RED" "$CRS" "$nblock" "$nwarn" "$RESET"
	exit 1
fi
if [ "$nwarn" -gt 0 ]; then
	printf '  %s%s%s no blocking findings, %d advisory\n\n' "$GRN" "$CHK" "$RESET" "$nwarn"
else
	printf '  %s%s%s clean\n\n' "$GRN" "$CHK" "$RESET"
fi
exit 0
