#!/usr/bin/env bash
# security_diff_test.sh — tests for the malicious-change gate (scripts/security-diff.sh).
#
# The gate's whole value is that it fails on things a text diff cannot show: a binary, a mode
# change, a symlink, a gitlink. None of those can be asserted by reading the script — a regex
# that no longer matches, or a `git diff --raw` field that moved by one column, looks exactly
# like a clean tree. So every case is planted in a throwaway repository and the gate is run
# against it for real.
#
# What is under test is the SEVERITY split, not just detection. A blocking case must exit 1; an
# advisory case must exit 0 while still printing. Getting that backwards in either direction is
# the failure that matters: a blocking bug lets a payload merge, and an advisory bug fails every
# Dependabot pull request until someone deletes the gate.
#
# Hermetic: its own git repo under a temp dir, removed on exit. Nothing here reads or writes the
# real checkout, so it is safe to run on a dirty tree.
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# The gate resolves its own repo root from $0 and cd's there, so it always scans the checkout it
# lives in. That is right in production and useless here, so the sandbox gets its own copy at the
# same relative path — the same trick verify_test.sh uses on verify.sh. $GATE is set once the
# temp repo exists.
SRC_GATE="$REPO/scripts/security-diff.sh"
GATE=""

TMP="$(mktemp -d "${TMPDIR:-/tmp}/secdifftest.XXXXXX")"
TMP="$(cd "$TMP" && pwd -P)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
ok() {
	printf '  \033[32m✓\033[0m %s\n' "$1"
	pass=$((pass + 1))
}
bad() {
	printf '  \033[31m✗\033[0m %s\n' "$1"
	fail=$((fail + 1))
}
# has REGEX / hasnt REGEX — grep the last captured $out. Same vocabulary as verify_test.sh.
has() { printf '%s' "${out:-}" | grep -qE -- "$1"; }
hasnt() { ! has "$1"; }

echo "== malicious-change gate =="

W="$TMP/w"
mkdir -p "$W"
cd "$W" || exit 1
git init -q .
git config user.email t@example.invalid
git config user.name t
git config commit.gpgsign false
# A global core.hooksPath is common on a maintainer's machine — this one carries a pre-commit
# hook that rejects any commit whose author is not the identity owning the checkout. That is
# correct for real work and fatal here, where every commit is a throwaway made by a fake author,
# so the sandbox is pointed at an empty hooks directory. Without this the commits fail, HEAD
# never moves, and every case reports a clean diff: the tests pass by checking nothing.
mkdir -p "$TMP/nohooks"
git config core.hooksPath "$TMP/nohooks"
mkdir -p src assets .github/workflows tools/tui scripts
cp "$SRC_GATE" scripts/security-diff.sh
chmod +x scripts/security-diff.sh
GATE="$W/scripts/security-diff.sh"
echo 'int main(void){return 0;}' > src/a.c
echo 'name: x' > .github/workflows/x.yml
echo '{"name":"t","dependencies":{"left-pad":"1.0.0"}}' > tools/tui/package.json
# assets/ has to exist in the base commit, or `git clean -fd` between cases removes it and the
# allowed-binary case plants into a directory that is no longer there.
echo 'placeholder' > assets/.keep
git add -A
git commit -qm base
BASE="$(git rev-parse HEAD)"

# reset_to_base — drop every commit and working-tree change made by the previous case, so the
# cases stay independent and can be read in any order.
# Reset first, then clean, and the order is the whole point. Cleaning first skips the gitlink
# because it is still tracked at that moment; the reset then drops it from the index but leaves
# vendor/thing on disk, where the next case's `git add -A` picks it straight back up as a
# gitlink and fails on a finding it did not plant. -ffd rather than -fd for the same reason: a
# single -f will not recurse into a nested git repository.
reset_to_base() {
	git reset -q --hard "$BASE" 2>/dev/null
	git clean -qffd 2>/dev/null
}

# run_case NAME EXPECTED-RC SETUP... — plant a change, commit it, run the gate over BASE..HEAD.
# $out is left holding the gate's combined output for the grep assertions below.
out=""
run_case() {
	local name="$1" want="$2"
	shift 2
	reset_to_base
	"$@"
	git add -A >/dev/null 2>&1
	git commit -qm "$name" >/dev/null 2>&1
	out="$(NO_COLOR=1 "$GATE" "$BASE" HEAD 2>&1)"
	local rc=$?
	if [ "$rc" = "$want" ]; then
		ok "$name (exit $rc)"
	else
		bad "$name: expected exit $want, got $rc"
		printf '%s\n' "$out" | sed 's/^/        /'
	fi
}

# ---- blocking cases: each must exit 1 -------------------------------------

plant_binary() { printf '\x7fELF\x02\x01\x01\x00\x00\x00' > src/payload.dat; }
run_case "binary file added outside assets/ blocks" 1 plant_binary
printf '%s' "$out" | grep -q 'binary file added' || bad "  ...and names the finding"

plant_exec() {
	printf 'data\n' > src/notascript
	chmod +x src/notascript
}
run_case "executable bit on a non-script blocks" 1 plant_exec

plant_symlink() { ln -s /etc/passwd src/link; }
run_case "symlink added blocks" 1 plant_symlink

# A real nested repository, not a hand-written index entry: `git add -A` in the parent is what
# turns this into a mode-160000 gitlink, and that is the path a contributor would actually take.
plant_gitlink() {
	mkdir -p vendor/thing
	(
		cd vendor/thing || exit
		git init -q .
		git config user.email t@example.invalid
		git config user.name t
		git config core.hooksPath "$TMP/nohooks"
		echo x > f
		git add -A
		git commit -qm inner
	) >/dev/null 2>&1
}
run_case "submodule gitlink added blocks" 1 plant_gitlink

plant_log() { printf 'ursk=deadbeef\n' > capture.log; }
run_case "flight-recorder .log committed blocks" 1 plant_log

plant_pcap() { printf 'x\n' > bench.pcapng; }
run_case "radio capture .pcapng committed blocks" 1 plant_pcap

plant_key() { printf -- '-----BEGIN PRIVATE KEY-----\n' > id.pem; }
run_case "private key file committed blocks" 1 plant_key

plant_big() { head -c 700000 /dev/zero | tr '\0' 'a' > src/big.txt; }
run_case "oversized file added blocks" 1 plant_big

# ---- advisory cases: each must exit 0, and still say something -------------

# The NUL matters: git calls a blob binary when it finds one in the first 8000 bytes, so a PNG
# header alone is still "text" to it and the binary branch never runs.
plant_asset_binary() { printf '\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\x00' > assets/pic.png; }
run_case "binary under assets/ warns but does not block" 0 plant_asset_binary
printf '%s' "$out" | grep -q 'allowed directory' \
	&& ok "  ...and says why it was allowed" \
	|| bad "  ...and says why it was allowed"

plant_workflow() { printf 'name: y\non: push\n' > .github/workflows/y.yml; }
run_case "workflow edit warns but does not block" 0 plant_workflow
printf '%s' "$out" | grep -q 'workflow changed' \
	&& ok "  ...and flags the token scope for review" \
	|| bad "  ...and flags the token scope for review"

plant_dep() { printf '%s' '{"name":"t","dependencies":{"left-pad":"1.0.0","chalk":"5.0.0"}}' > tools/tui/package.json; }
run_case "new dependency warns but does not block" 0 plant_dep
printf '%s' "$out" | grep -q 'dependency manifest changed' \
	&& ok "  ...and names the manifest" \
	|| bad "  ...and names the manifest"

plant_url() { printf 'fetch("https://evil.example.com/x")\n' > src/f.js; }
run_case "new remote URL warns but does not block" 0 plant_url
printf '%s' "$out" | grep -q 'new remote URL' \
	&& ok "  ...and lists the URL" \
	|| bad "  ...and lists the URL"

plant_script() {
	printf '#!/bin/sh\necho hi\n' > src/tool.sh
	chmod +x src/tool.sh
}
run_case "executable *.sh is normal, no finding" 0 plant_script

plant_source() { printf 'int b(void){return 1;}\n' > src/b.c; }
run_case "ordinary source addition is clean" 0 plant_source
printf '%s' "$out" | grep -q 'clean' \
	&& ok "  ...and reports clean" \
	|| bad "  ...and reports clean"

# ---- usage ----------------------------------------------------------------

reset_to_base
NO_COLOR=1 "$GATE" definitely-not-a-rev HEAD >/dev/null 2>&1
[ $? = 2 ] && ok "an unresolvable base exits 2, not 0" || bad "an unresolvable base exits 2, not 0"

# ---- working-tree mode -----------------------------------------------------
# Called with fewer than two revisions, the gate compares the base against the working tree and
# also walks untracked files. This is the mode `make security` and `make verify` use, and it is
# where the gate was originally blind: with nothing committed ahead of the base it diffed a
# commit against itself, examined zero files, and printed a tick. Every case here would have
# passed vacuously before that fix, so they are the regression tests for it.

# run_wt NAME EXPECTED-RC SETUP... — plant a change and DO NOT commit it.
run_wt() {
	local name="$1" want="$2"
	shift 2
	reset_to_base
	"$@"
	out="$(NO_COLOR=1 "$GATE" "$BASE" 2>&1)"
	local rc=$?
	if [ "$rc" = "$want" ]; then
		ok "$name (exit $rc)"
	else
		bad "$name: expected exit $want, got $rc"
		printf '%s\n' "$out" | sed 's/^/        /'
	fi
}

run_wt "untracked binary blocks without being staged" 1 plant_binary
has 'binary file added' && ok "  ...and names it" || bad "  ...and names it"

run_wt "untracked .log blocks without being staged" 1 plant_log

plant_untracked_symlink() { ln -s /etc/passwd src/sneaky; }
run_wt "untracked symlink blocks" 1 plant_untracked_symlink

plant_unstaged_edit() { printf 'int b(void){return 2;}\n' >> src/a.c; }
run_wt "an uncommitted edit is examined, not skipped" 0 plant_unstaged_edit
has 'examining' && ok "  ...and the scope line reports a file count" \
	|| bad "  ...and the scope line reports a file count"

# The regression itself: a dirty tree with nothing committed must never report "nothing to
# examine". That sentence is only correct when the tree really is clean.
run_wt "a dirty tree is never reported as nothing to examine" 1 plant_binary
hasnt 'nothing to examine' && ok "  ...and does not claim there was nothing to look at" \
	|| bad "  ...and does not claim there was nothing to look at"

reset_to_base
out="$(NO_COLOR=1 "$GATE" "$BASE" 2>&1)"
has 'nothing to examine' && ok "a genuinely clean tree does say nothing to examine" \
	|| bad "a genuinely clean tree does say nothing to examine"

printf '\n  %d passed, %d failed\n\n' "$pass" "$fail"
[ "$fail" = 0 ]
