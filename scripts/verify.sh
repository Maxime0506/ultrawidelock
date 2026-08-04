#!/usr/bin/env bash
#
# Pre-push sweep: every CI gate that a host can run, in one shot.
#
# The point of this script is that "it passed locally" and "it will pass CI"
# mean the same thing. Each row below is one CI *job* (not one workflow —
# one job in ci.yml now runs all of them), running the same
# command that job runs. Adding a job to .github/workflows/ without adding it
# here re-opens the gap this script exists to close.
#
# Out of scope, deliberately: firmware-builds.yml and release.yml. They need
# ESP-IDF and NCS (~6.5 GB of toolchain) and take tens of minutes — not a push
# gate. `make build` covers them once the toolchain is bootstrapped.
#
# The gates run in lanes, several at once, because serially they are ~83s of
# work on a machine with eight cores. A short serial tripwire goes first, so a
# formatting slip still stops the sweep about four seconds in; then the
# expensive gates run together and the sweep costs its slowest lane rather than
# the sum of all of them. Measured back to back on an idle host: 83s serial,
# 34s in lanes, and 72s in lanes with cbmc on against 147s serial.
#
# SERIAL=1 puts it back to one gate at a time, for a busy machine or for reading
# a confusing failure in order.
#
# One gate does not run by default: cbmc. At 64s it is twice the rest of the
# sweep put together, spent on the gate whose input moves least — the wire
# parsers it proves have been stable for months, and the fuzz gate exercises the
# same code every run. WITH_CBMC=1 turns it on, taking the sweep to ~72s.
#
# It still gets a summary row saying it did not run: a gate that quietly
# disappears from the sweep is the exact failure this script exists to prevent.
# The PR runs it whenever the branch touches what it proves — which since the
# path filter below is a narrower claim than this comment used to make, and the
# reason WITH_CBMC=1 in CI is no longer the same as "on every pull request".
#
# A gate whose tool is missing FAILS the sweep. It says so on its row, it is
# counted apart from a hand-scoped SKIP=, and the run exits nonzero. Anything
# softer is the original bug wearing a warning label: CI runs that gate whatever
# this host has installed, so "could not check" has to read as "not verified",
# not as "fine". `make tools-install` is the fix; SKIP="<gate>" is the override
# for someone who has decided to accept the gap.
#
# Most gates only read part of the tree, so most changes cannot break most of
# them. A gate whose inputs this branch does not touch is skipped with a row
# saying so — see the path-filter section below for how that is decided, and for
# the four conditions that turn the whole thing off and sweep everything.
#
# Env:
#   WITH_CBMC=1        also run the cbmc proof (off by default, see above)
#   SERIAL=1           one gate at a time, fail-fast, instead of lanes
#   SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
#   FILTER=0           run every gate whatever changed, ignoring the path filter
#   FILTER_BASE=<ref>  what "changed" is measured against. Unset means
#                      origin/main; set-but-empty means there is no base, and
#                      the filter is off.
#   COV_MIN=90         line-coverage floor. Reported, never blocking: under it the
#                      row still passes and says so. Raise it to aim higher.
#   NO_COLOR=1         plain output (colour is the default, pipe or not)
#   FAIL_TAIL=40       lines of a failing gate's log to show inline
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

# Brings $PY (the interpreter the python suites run under, repo-local .venv when
# present) plus the source lists the clang-tidy gate compiles against.
# shellcheck disable=SC1091  # repo-local, sourced for its arrays and $PY
. "$ROOT/tests/host/sources.sh"

COV_MIN="${COV_MIN:-90}"
SKIP="${SKIP:-}"
FAIL_TAIL="${FAIL_TAIL:-40}"

# ---- glyphs + colour (same vocabulary as scripts/test-runner.sh) -----------
# Colour is on unless NO_COLOR says otherwise — deliberately not gated on a tty.
# The usual `[ -t 1 ]` test gets this backwards here: the pre-push hook runs the
# sweep through git-pr, which captures stdout, so the one place the reader most
# needs to spot a red row at a glance is exactly the place tty detection turns
# the colour off. Nothing in CI runs this script, and the one caller that wants
# plain output (the sandbox in tests/tooling/verify_test.sh) sets NO_COLOR=1.
if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' CYAN=$'\033[36m' GRN=$'\033[32m'
	RED=$'\033[31m' YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" ARR="→" TIL="~" DOT="·"
	# 58 wide: the row printf is 2 + glyph + 1 + 12 + 1 + 36 + 4 + the two
	# leading spaces the rule is printed with, so the rule ends where a row does.
	HR="──────────────────────────────────────────────────────────"
	HR4="────"
else
	BOLD="" DIM="" CYAN="" GRN="" RED="" YEL="" RESET=""
	CHK="+" CRS="x" ARR=">" TIL="~" DOT="*"
	HR="----------------------------------------------------------"
	HR4="----"
fi

# ---- gate table -----------------------------------------------------------
# One row per CI job, with the wall time each takes alone on an 8-core Apple
# silicon host with warm caches. This order is the summary's, not the run's:
# what runs when is set by TRIPWIRE and LANES below. The times are what those
# are packed against, so a gate that gets much slower wants repacking.
# Timings are a warm run on an 8-core laptop. The right-hand column is where the
# gate runs in CI: ci.yml is one job that runs this whole sweep, so almost every
# row says the same thing now -- before the CI consolidation each of these was
# its own workflow and its own runner. The exceptions are the interesting part.
GATES=(
	test-web    # 0s   ci.yml : verify
	actionlint  # 0s   ci.yml : verify
	zizmor      # 0s   ci.yml : verify
	mal-diff    # 0s   ci.yml : verify
	esp         # 0s   ci.yml : verify
	attest      # 0s   ci.yml : verify
	approtect   # 0s   ci.yml : verify   (source layer only in CI, which builds no firmware; the
	            #                         generated-.config layer runs on a contributor's tree)
	uwb-seam    # 0s   ci.yml : verify
	ct          # 0s   ci.yml : verify   (0s only because it SKIPS: no valgrind on darwin/arm64;
	            #                         17s in CI, where valgrind is installed)
	format      # 1s   ci.yml : verify
	shellcheck  # 1s   ci.yml : verify
	secrets     # 2s   ci.yml : verify
	web         # 7s   ci.yml : verify   (retire fetches its advisory repo, so it wants network)
	licenses    # 3s   ci.yml : verify
	fuzz        # 3s   ci.yml : verify
	clang-tidy  # 4s   ci.yml : verify
	test        # 5s   ci.yml : verify
	twin-wasm   # 7s   LOCAL ONLY        (2s warm, 7s cold; emsdk is a ~1 GB install, so ci.yml
	            #                         SKIPs it. test-web still checks the committed twin.js.)
	test-tui    # 9s   LOCAL ONLY        (needs bun. The TUI stopped shipping in releases when
	            #                         they became firmware-only, so no CI job builds it; a
	            #                         contributor sweep is where it is checked.)
	test-san    # 8s   ci.yml : verify
	patch-drift # 11s  ci.yml : verify
	cdk-size    # 1s   ci.yml : verify   (the accounting rule and the comparator's refusals, on
	            #                         fixtures: CI builds no firmware, so it cannot measure one)
	docs        # 12s  ci.yml : verify   (docs.yml renders and publishes the site from main;
	            #                         this is the same drift + link check, run per PR)
	deps        # 13s  ci.yml : verify   (pip-audit queries PyPI, so it waits on network)
	test-port   # 14s  ci.yml : verify
	test-ws     # 14s  LOCAL ONLY        (ws-seed clones with cp -c and fails loudly off APFS by
	            #                         design, so a Linux runner cannot test it)
	test-verify # 15s  ci.yml : verify
	coverage    # 18s  ci.yml : verify   (the line floor is advisory, not blocking)
	semgrep     # 22s  ci.yml : verify   (registry packs are fetched, so it needs network)
	cbmc        # 82s  ci.yml : verify   (opt-in locally via WITH_CBMC=1; ci.yml sets it)
)

# ---- lanes ----------------------------------------------------------------
# Run serially the table above is 103s of work on a machine with eight cores,
# which is mostly idle waiting. Lanes run at once; gates inside a lane run in
# order. Two rules set the shape:
#
#   1. Gates that write the same path must share a lane. Exactly one pair does:
#      `test` and `test-san` are the same run.sh writing the same
#      build/host/host_test* binaries, so side by side they would overwrite each
#      other's build with no error at all. twin-wasm and docs share one for a
#      softer reason: docs renders the twin, so it should see the rebuilt
#      twin.js, which is the order the old serial sweep happened to have.
#   2. Everything else is already hermetic and was checked one at a time:
#      test-ws and patch-drift build under `mktemp -d`, test-port under
#      `mktemp -t` per binary, coverage under build/host/coverage/, fuzz under
#      build/host/fuzz/, cbmc writes only its own logs. The lint gates only read.
#
# Order WITHIN a lane matters for one reason: a lane stops at its first failure,
# so everything after it reports "not run". That is why secrets and deps sit
# ahead of test-tui rather than after it — they cost three seconds between them,
# and putting them behind the longest gate in their lane meant a broken bun
# toolchain silently took the two security scanners down with it.
#
# TRIPWIRE runs first and serially, and a failure there stops the sweep. It is
# the whole sub-2s set, so a formatting slip costs four seconds instead of the
# full run: the fail-fast the parallel phase gives up, bought back where it is
# cheap. It also runs the two whole-tree scanners (licenses, format) before
# anything starts writing, so neither reads a file mid-rewrite.
TRIPWIRE="test-web actionlint zizmor format shellcheck licenses mal-diff esp attest approtect uwb-seam"
#
# Packed, not one lane per gate. coverage sets the floor at ~25s and nothing
# finishes before it, so lanes past that buy nothing and cost real time: one
# lane per gate measured 75s against 34s packed, and burned more than twice the
# CPU to do it. Packing to roughly the floor leaves the machine some headroom.
#
# Repack by the times in the gate table when they drift, and re-measure rather
# than reason about it: a run under Spotlight indexing said cbmc cost 234s in a
# lane, which is three times what a quiet machine says, and a schedule was
# designed around that number before the second measurement caught it.
LANES=(
	"coverage"                 # 25s  the floor: nothing finishes before this
	"semgrep"                  # 22s  own lane: the one gate that waits on the network
	"test-ws patch-drift cdk-size" # 23s  the tests/tooling/ scripts; cdk-size adds ~1s
	"twin-wasm docs clang-tidy" # 19s  rebuild the twin before docs renders it
	"test-port fuzz"           # 17s
	"secrets deps test-tui"    # 12s  the two read-only scanners first, then bun
	"test test-san"            # 15s  same run.sh, same build/host/host_test* paths
	"test-verify web ct"       # 22s  ct costs 0s here (it skips); web is retire's network fetch
	"cbmc"                     # 64s  WITH_CBMC=1 only, and then it is the floor
)

# Every gate placed exactly once. A gate dropped from both lists would keep its
# row in the table above and quietly never run, which is precisely the failure
# this script exists to catch, so it is checked rather than trusted.
placed="$TRIPWIRE ${LANES[*]}"
misplaced=""
for g in "${GATES[@]}"; do
	cnt=0
	for p in $placed; do [ "$p" = "$g" ] && cnt=$((cnt + 1)); done
	[ "$cnt" = 1 ] || misplaced="${misplaced:+$misplaced }$g(in $cnt lanes)"
done
for p in $placed; do
	case " ${GATES[*]} " in
	*" $p "*) ;;
	*) misplaced="${misplaced:+$misplaced }$p(not a gate)" ;;
	esac
done
if [ -n "$misplaced" ]; then
	printf 'verify.sh: lane assignment does not cover the gate table: %s\n' \
		"$misplaced" >&2
	exit 2
fi

# ---- path filter ----------------------------------------------------------
# What each gate reads, as git pathspecs. A gate is skipped when this branch
# touches none of them. The bias is deliberately toward running: an EMPTY list
# means "no filter, always run", so a gate added to the table above without a
# row here keeps running rather than silently disappearing, which is the failure
# mode this whole script exists to prevent. Widen a row when in doubt — an extra
# minute of CI is cheap next to a gate that stopped watching its own inputs.
#
# The always-run set is not a leftover. secrets, mal-diff and licenses read the
# diff itself or the whole tree, so "which files changed" is their input rather
# than a filter on it; docs and patch-drift are cheap and break from directions
# their own paths do not predict (a moved anchor, a re-pinned upstream).
gate_paths() { # <gate>
	case "$1" in
	# Whole-tree or diff-shaped: the change IS the input.
	secrets | mal-diff | licenses | docs | patch-drift) echo "" ;;

	format) echo "modules/" ;;
	shellcheck) echo "*.sh" ;;
	actionlint | zizmor) echo ".github/workflows/" ;;
	attest) echo ".github/workflows/ security/" ;;
	esp) echo "ports/esp32/ security/" ;;
	approtect) echo "firmware/ ports/ security/ scripts/check-approtect.sh" ;;
	uwb-seam) echo "modules/woz_uwb/ security/ scripts/check-uwb-seam.sh" ;;
	cdk-size) echo "firmware/ scripts/ tests/tooling/" ;;
	test-web) echo "web-twin/ web-flasher/ modules/ firmware/ ports/esp32/" ;;
	twin-wasm) echo "web-twin/" ;;
	web) echo "web-twin/ activity/ integration/ tools/ security/" ;;
	deps) echo "activity/ tools/tui/ integration/homeassistant/ ports/esp32/ web-twin/ security/" ;;
	ct) echo "modules/ tests/host/" ;;
	# The C core and its harnesses. cbmc and fuzz are the two most expensive
	# gates in the sweep and the two whose input moves least, which is the whole
	# reason this filter is worth having.
	test | test-san | coverage | clang-tidy | fuzz | cbmc) echo "modules/ tests/host/ deps/" ;;
	test-port) echo "ports/esp32/ modules/" ;;
	test-ws) echo "scripts/ mk/ Makefile" ;;
	test-tui) echo "tools/tui/" ;;
	# Its subject is the sweep itself, and FILTER_ALWAYS below already forces a
	# full run whenever any of that moves. Listed anyway so the row is explicit.
	test-verify) echo "scripts/ tests/tooling/ mk/ .github/workflows/" ;;
	# SAST over the whole tree, so its pathspec is every directory that holds
	# code. Prose is the only thing it cannot have an opinion about.
	semgrep) echo "modules/ ports/ firmware/ tools/ scripts/ host/ integration/ web-twin/ tests/ deps/ security/ .github/" ;;
	# The fail-safe: a gate added above and forgotten here runs every time.
	*) echo "" ;;
	esac
}

# Touching any of these means the filter itself, or a gate's definition, may
# have moved — and then a filtered sweep is reasoning with a stale map. The
# answer is not a cleverer filter, it is to stop filtering: everything runs.
FILTER_ALWAYS="scripts/ mk/ Makefile .github/workflows/ tests/tooling/ tests/host/sources.sh security/"

# Resolve what "changed" is measured against. Failing to resolve is not an
# error and not a reason to guess: it disables the filter and sweeps everything,
# which is exactly what this script did before the filter existed.
FILTER="${FILTER:-1}"
# Unset-only, deliberately: FILTER_BASE= set to the empty string has to stay
# empty. ci.yml sets it to "" on push and workflow_dispatch to mean "there is no
# base here, sweep everything", and a :- default would quietly rewrite that into
# origin/main — which is right by accident on a push to main (the merge base is
# HEAD, so the empty-diff rule sweeps anyway) and wrong on a workflow_dispatch
# from a branch, where it would silently filter against main instead.
FILTER_BASE="${FILTER_BASE-origin/main}"
FILTER_OFF="" # non-empty = full sweep, and this says why

if [ "$FILTER" = 0 ]; then
	FILTER_OFF="FILTER=0"
elif [ -z "$FILTER_BASE" ]; then
	# Handled here rather than left to merge-base: the reason a reader sees
	# should say no base was given, not "no merge base with ".
	FILTER_OFF="no base given"
elif ! command -v git >/dev/null 2>&1 || ! git rev-parse --git-dir >/dev/null 2>&1; then
	FILTER_OFF="not a git checkout"
elif ! FILTER_MB="$(git merge-base "$FILTER_BASE" HEAD 2>/dev/null)" || [ -z "$FILTER_MB" ]; then
	FILTER_OFF="no merge base with $FILTER_BASE"
else
	FILTER_BASE="$FILTER_MB"
fi

# Changed paths matching a pathspec: committed since the merge base, plus
# whatever is uncommitted or untracked right now. A pre-push sweep is asked
# about the tree in front of it, not only about what is already committed.
#
# Takes ONE argument: the whole space-separated pathspec list. It is split here,
# with globbing off, rather than at the call site — `*.sh` is a pathspec for git
# to interpret, and an unquoted expansion would let the shell match it against
# the working directory first. Today nothing at the repository root ends in .sh
# so it survives by luck; the day something does, the shellcheck gate would
# quietly narrow to that one file. Splitting under `set -f` removes the luck.
filter_touched() { # <pathspec-list>
	set -f
	# shellcheck disable=SC2086  # deliberate split of a pathspec list, globbing off
	set -- $1
	set +f
	[ "$#" -gt 0 ] || return 0
	{
		git diff --name-only "$FILTER_BASE" -- "$@" 2>/dev/null
		git ls-files --others --exclude-standard -- "$@" 2>/dev/null
	} | head -n 1
}

if [ -z "$FILTER_OFF" ] && [ -n "$(filter_touched "$FILTER_ALWAYS")" ]; then
	FILTER_OFF="the sweep's own machinery changed"
fi

# Nothing differs from the base at all. This is the push to main after a merge,
# and it is the one case where filtering is actively wrong: the merge base is
# HEAD, every diff is empty, and a filter that trusted that would skip all 30
# gates and call the sweep green. An empty diff means "no information", not "no
# risk", so it sweeps everything — which is also the post-merge safety net.
if [ -z "$FILTER_OFF" ] && [ -z "$(filter_touched ".")" ]; then
	FILTER_OFF="nothing differs from the base"
fi

# 0 = this branch touches nothing the gate reads, so it is skipped.
gate_unchanged() { # <gate>
	local paths
	[ -z "$FILTER_OFF" ] || return 1
	paths="$(gate_paths "$1")"
	[ -n "$paths" ] || return 1
	[ -z "$(filter_touched "$paths")" ]
}

# FILTER_EXPLAIN=1 prints what the filter would decide and runs nothing. A
# filter you cannot interrogate is one nobody will trust enough to keep on, and
# "why did that gate not run" needs an answer cheaper than reading this file.
if [ -n "${FILTER_EXPLAIN:-}" ]; then
	if [ -n "$FILTER_OFF" ]; then
		printf 'filter OFF — %s — all %d gates run\n' "$FILTER_OFF" "${#GATES[@]}"
	else
		printf 'filter ON — changes vs %s\n' "$FILTER_BASE"
	fi
	for g in "${GATES[@]}"; do
		if gate_unchanged "$g"; then
			printf '  skip  %-12s  %s\n' "$g" "$(gate_paths "$g")"
		else
			printf '  RUN   %-12s  %s\n' "$g" "$(gate_paths "$g")"
		fi
	done
	exit 0
fi

# What each gate needs on PATH. Empty = nothing beyond a shell and a compiler.
# A bash-3.2 case function, not an associative array: macOS ships bash 3.2 and
# tests/host/fuzz.sh already sets this precedent.
gate_need() {
	case "$1" in
	format) echo "clang-format" ;;
	shellcheck) echo "shellcheck" ;;
	test-web) echo "python3" ;;
	test-verify) echo "python3" ;; # its sandbox runs the real floor + licence checks
	actionlint) echo "actionlint" ;;
	twin-wasm) echo "node" ;; # emcc is resolved from ~/emsdk by twin-wasm.sh
	docs) echo "doxygen dot" ;;
	coverage) echo "python3" ;;
	clang-tidy) echo "clang-tidy" ;;
	zizmor) echo "zizmor" ;;
	licenses) echo "reuse" ;;
	cbmc) echo "cbmc" ;;
	test-tui) echo "bun" ;;
	secrets) echo "gitleaks" ;;
	semgrep) echo "semgrep" ;;
	web) echo "retire" ;;
	# ct needs valgrind, which has no darwin/arm64 build at all. It is deliberately NOT listed:
	# every other row here fails the sweep when its tool is missing, on the argument that "could
	# not check" must not read as "fine". That argument still holds, but `make tools-install`
	# cannot answer it on this platform, so the gate returns 2 and gets a skip-tool row instead.
	ct) echo "" ;;
	esp | attest) echo "" ;;
	# pip-audit is the second half of this gate and is checked inside
	# scripts/security.sh, which fails rather than skipping when it is absent.
	deps) echo "osv-scanner pip-audit" ;;
	*) echo "" ;;
	esac
}

# Python packages a gate's suites import. `command -v` cannot see these: they
# are modules inside an interpreter, not binaries on PATH, which is exactly how
# they went unnoticed. Absent, the suites still run and still report success,
# having quietly skipped the checks that need them — ci.yml installs
# both, so CI runs those checks whatever this host has.
gate_need_py() {
	case "$1" in
	test) echo "markdown" ;;            # test_flash_html: 11 checks
	coverage) echo "markdown coverage" ;; # the same suite, under measurement
	*) echo "" ;;
	esac
}

# Return the human-readable label for a CI gate name.
# Labels are used in the summary row at the end of the verify sweep.
gate_label() {
	case "$1" in
	format) echo "clang-format over modules/" ;;
	shellcheck) echo "shellcheck every tracked *.sh" ;;
	test-web) echo "twin constants and flasher codes match the firmware" ;;
	actionlint) echo "workflow syntax" ;;
	fuzz) echo "wire-parser fuzz corpus" ;;
	test) echo "host KAT suite" ;;
	twin-wasm) echo "rebuild WASM twin + node selftest" ;;
	patch-drift) echo "vendored patches still apply" ;;
	cdk-size) echo "CDK size gate reproduces the linker" ;;
	docs) echo "site builds, no dead links" ;;
	test-san) echo "host suite under ASan + UBSan" ;;
	test-port) echo "ESP32 port tests" ;;
	test-ws) echo "workspace auto-seeding" ;;
	test-tui) echo "guided bench types, tests, build" ;;
	test-verify) echo "this sweep's own tests" ;;
	# The measured number goes on the row, not just into the gate's log. Now that
	# the floor is advisory this row is a green one, and a passing gate's log is
	# never printed -- so the log is where the number would go to die. Reading the
	# summary the gate just wrote is what keeps an advisory gate from becoming a
	# decorative one. Before the gate has run there is no file, and the label
	# falls back to describing itself.
	coverage)
		local cov_label
		cov_label="$(COV_MIN="$COV_MIN" \
			COV_SUMMARY="${ALIRO_BUILD_ROOT:-build}/host/coverage/summary.json" \
			python3 -c '
import json, os, sys
try:
    pct = float(json.load(open(os.environ["COV_SUMMARY"]))["data"][0]["totals"]["lines"]["percent"])
except Exception:
    sys.exit(1)
floor = float(os.environ["COV_MIN"])
if pct < floor:
    print("line coverage %.2f%% < %.0f%% advisory" % (pct, floor))
else:
    print("line coverage %.2f%% (floor %.0f%%)" % (pct, floor))' 2>/dev/null || true)"
		[ -n "$cov_label" ] || cov_label="line coverage (advisory floor ${COV_MIN}%)"
		echo "$cov_label"
		;;
	clang-tidy) echo "static analysis of the core" ;;
	zizmor) echo "workflow security audit" ;;
	licenses) echo "licence store is consistent" ;;
	cbmc) echo "wire-parser memory-safety proof" ;;
	secrets) echo "no secrets in the tracked files" ;;
	web) echo "browser supply chain: pins, CSP, installs" ;;
	ct) echo "no secret-dependent branches" ;;
	approtect) echo "no image locks APPROTECT" ;;
	uwb-seam) echo "no caller bypasses the CCC STS seam" ;;
	esp) echo "ESP component pins are exact" ;;
	attest) echo "release provenance configured" ;;
	mal-diff) echo "no malicious change shapes" ;;
	semgrep) echo "SAST over the whole tree" ;;
	deps) echo "no vulnerable or malicious deps" ;;
	esac
}

# The gates that dispatch through scripts/security.sh. One list, because run_gate needs it too:
# only this family uses an exit status of 2 to mean "this host cannot answer the question", and
# reading that status the same way everywhere else would be wrong. docs is the example — it exits
# 2 for "your branch is behind origin/main", which is a real problem with an obvious fix, not a
# gap in the host.
gate_is_security() { # <gate>
	case "$1" in
	secrets | mal-diff | semgrep | deps | web | ct | esp | attest) return 0 ;;
	*) return 1 ;;
	esac
}

# The command each gate runs. Where CI runs a make target, so do we; where CI
# runs a raw command, this reproduces it verbatim.
gate_run() {
	# The eight security gates are one script, which is also what ci.yml runs and what
	# `make security` runs. No environment is set: with no SECURITY_BASE, `secrets` scans every
	# tracked file and `mal-diff` compares against the merge base with origin/main, which is the
	# pre-push question ("what does this branch add?"). CI passes the pull request's base and
	# head instead.
	if gate_is_security "$1"; then
		scripts/security.sh "$1"
		return
	fi
	case "$1" in
	format)
		git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' \
			| xargs clang-format --dry-run --Werror
		;;
	shellcheck) git ls-files '*.sh' | xargs shellcheck -S warning ;;
	# Its own --self-test runs first, so a run that reports "ok" has proved the
	# detector still fires before it claims the tree is clean.
	approtect) scripts/check-approtect.sh --self-test && scripts/check-approtect.sh ;;
	uwb-seam) scripts/check-uwb-seam.sh --self-test && scripts/check-uwb-seam.sh ;;
	test-web) make --no-print-directory test-web ;;
	actionlint) actionlint -color ;;
	fuzz) make --no-print-directory fuzz ;;
	test) make --no-print-directory test ;;
	twin-wasm)
		# CI runs the node selftest against the committed twin.js,
		# rebuilds, and runs it again. The rebuild diff is warn-only there
		# (emsdk binaries differ across host OSes), so it is warn-only here.
		node web-twin/selftest.cjs \
			&& make --no-print-directory twin-wasm \
			&& node web-twin/selftest.cjs \
			&& { git diff --exit-code --stat -- web-twin/twin.js \
				|| printf '  note: twin.js differs from this rebuild (warn-only, as in CI)\n'; }
		;;
	patch-drift) tests/tooling/patch_drift_check.sh ;;
	cdk-size) tests/tooling/cdk_size_test.sh ;;
	docs) make --no-print-directory docs ;;
	test-san) make --no-print-directory test-san ;;
	# Host layers only. Its third layer, verify_port.sh, shells out to `idf.py
	# build` whenever ESP-IDF is sourced -- which ci.yml's runner never
	# is, so CI does not run it either. Left alone it would drop a multi-minute
	# firmware build into a 33s sweep, from a shell state the sweep cannot see.
	test-port) WOZ_NO_TARGET_BUILD=1 make --no-print-directory test-port ;;
	test-ws) make --no-print-directory test-ws ;;
	# All three steps release.yml runs, in its order. `make tui-test` alone would
	# pass a branch whose types are broken or whose executable does not link,
	# because CI only finds those in the typecheck and release steps.
	# The install is part of the gate, exactly as release.yml runs it. Without it this gate read
	# whatever node_modules the developer happened to leave behind: a plain `bun install` omits
	# the other platforms' optional binaries, and `bun run release` cross-compiles for linux-x64
	# as well as darwin-arm64, so the gate failed with "Could not resolve
	# @opentui/core-linux-x64" on a tree that was completely fine. A gate whose answer depends on
	# ambient state nothing establishes is not a gate.
	test-tui)
		(cd "$ROOT/tools/tui" \
			&& bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*' \
			&& bun run typecheck && bun run test && bun run release)
		;;
	test-verify) make --no-print-directory test-verify ;;
	coverage)
		# The floor is ADVISORY: coming in under it does not block, here or on
		# CI. There is no second place to change -- ci.yml runs this same sweep
		# (`make verify`), so this branch was the only thing enforcing it, and an
		# older comment here claiming CI enforced it separately was wrong.
		#
		# What still fails is the gate failing to do its job: the suite not
		# building, or a summary that is missing or unreadable. That distinction
		# is the whole point -- "measured, and it came in at 89.8%" is a number
		# to act on, while "could not measure" is not verified, and the rule
		# above (a missing tool FAILS) applies to it unchanged.
		make --no-print-directory coverage || return 1
		COV_MIN="$COV_MIN" python3 -c '
import json, os, sys
path = os.environ.get("ALIRO_BUILD_ROOT", "build") + "/host/coverage/summary.json"
try:
    totals = json.load(open(path))["data"][0]["totals"]
    pct = float(totals["lines"]["percent"])
except Exception as exc:
    print("  no readable coverage summary at %s (%s)" % (path, exc))
    sys.exit(1)
floor = float(os.environ["COV_MIN"])
note = "" if pct >= floor else "  <-- BELOW FLOOR (advisory, does not block)"
print("  total line coverage %.2f%% (floor %.0f%%)%s" % (pct, floor, note))'
		;;
	clang-tidy)
		# On macOS the SDK headers are not on the default search path, so
		# without -isysroot this fails on a missing <string.h> — a tooling
		# error dressed up as a finding. CI (Linux) needs no such flag.
		local sysroot=()
		if [ "$(uname -s)" = Darwin ]; then
			sysroot=(-isysroot "$(xcrun --show-sdk-path)")
		fi
		clang-tidy --quiet "${UNIT_SRCS[@]}" -- \
			-std=c11 "${sysroot[@]}" "${DEFS[@]}" "${INCS[@]}"
		;;
	zizmor) zizmor .github/workflows ;;
	licenses)
		# The licences gate covers the licence *store*, not full REUSE compliance:
		# most of this tree predates the per-file header convention, so a bare
		# `reuse lint` exits nonzero where CI passes. CI tolerates that exit and
		# filters the JSON down to six categories. Reproduce that, not the
		# stricter thing the command does by default.
		local rj rc
		rj="$(mktemp -t oa-reuse.XXXXXX)"
		reuse lint --json >"$rj" 2>/dev/null || true
		REUSE_JSON="$rj" python3 -c '
import json, os, sys
nc = json.load(open(os.environ["REUSE_JSON"]))["non_compliant"]
gated = ("missing_licenses", "unused_licenses", "bad_licenses",
         "deprecated_licenses", "licenses_without_extension", "read_errors")
bad = {k: nc[k] for k in gated if nc.get(k)}
for k in ("missing_licensing_info", "missing_copyright_info"):
    label = k.split("_")[1]
    print(f"  note: {len(nc.get(k, []))} file(s) without {label} header")
if bad:
    for k, v in bad.items():
        print(f"  FAIL {k}: {v}", file=sys.stderr)
    sys.exit(1)
print("  licence store consistent")'
		rc=$?
		rm -f "$rj"
		return "$rc"
		;;
	cbmc) make --no-print-directory cbmc ;;
	esac
}

# ---- execution ------------------------------------------------------------
# Lanes run as background subshells, so they cannot report back through shell
# variables. $RUNDIR is the channel: one <gate>.rc per finished gate holding
# "status<TAB>seconds<TAB>reason", and one <gate>.out with its output. Written
# to a .tmp and renamed, because the parent reads these while lanes are still
# running and a half-written line would be read as a status.
if ! RUNDIR="$(mktemp -d -t oa-verify.XXXXXX)"; then
	printf 'verify.sh: could not create the result directory\n' >&2
	exit 2
fi
if [ -z "$RUNDIR" ] || [ ! -d "$RUNDIR" ] || [ ! -w "$RUNDIR" ]; then
	printf 'verify.sh: result directory is unavailable: %s\n' "${RUNDIR:-<empty>}" >&2
	exit 2
fi
# Kept only when a gate failed: the summary prints the path to its full log, so
# deleting it on the way out would hand the reader a path to nothing.
KEEPDIR=""
trap '[ -n "$KEEPDIR" ] || rm -rf "$RUNDIR"' EXIT

# Write the result of a gate to a temporary file in RUNDIR and atomically rename
# it, recording status (0 passed, 1 failed, 2 skipped), elapsed seconds, and an
# optional reason string. Called from inside a lane subshell; the summary reads
# these files after all lanes join. Losing this record must fail the lane: a
# missing result can never be treated as a passing gate.
gate_result() { # <gate> <status> <secs> <reason>
	if ! printf '%s\t%s\t%s\n' "$2" "$3" "$4" >"$RUNDIR/$1.rc.tmp"; then
		printf 'verify.sh: could not record result for %s\n' "$1" >&2
		return 1
	fi
	if ! mv -f "$RUNDIR/$1.rc.tmp" "$RUNDIR/$1.rc"; then
		printf 'verify.sh: could not publish result for %s\n' "$1" >&2
		rm -f "$RUNDIR/$1.rc.tmp"
		return 1
	fi
}

# Prints the gate's row as it finishes. Concurrent lanes write these
# interleaved, which is fine: each row is a single printf, and the summary
# below is rebuilt from the .rc files rather than from what was printed.
gate_row() { # <gate> <status> <secs> <reason>
	case "$2" in
	pass) printf '  %s%s%s %-12s %s%-36s%4ds%s\n' \
		"$GRN" "$CHK" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$3" "$RESET" ;;
	fail) printf '  %s%s%s %-12s %s%-36s%4ds  FAILED (%s)%s\n' \
		"$RED" "$CRS" "$RESET" "$1" "$RED" "$(gate_label "$1")" "$3" "$4" "$RESET" ;;
	skip-req) printf '  %s%s%s %-12s %s%-36s%sskipped via SKIP=%s\n' \
		"$YEL" "$TIL" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$YEL" "$RESET" ;;
	skip-optin) printf '  %s%s%s %-12s %s%-36s%sskipped — opt-in, WITH_CBMC=1 make verify%s\n' \
		"$YEL" "$TIL" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$YEL" "$RESET" ;;
	# Dim, not yellow, and no warning anywhere: unlike every other skip here this
	# one is not a gap. CI applies the same filter to the same diff, so there is
	# no local-versus-CI divergence for the reader to worry about.
	skip-path) printf '  %s%s%s %-12s %s%-36sunchanged — nothing it reads was touched%s\n' \
		"$DIM" "$TIL" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$RESET" ;;
	skip-tool) printf '  %s%s%s %-12s %s%-36s%sSKIPPED — %s (CI still runs it)%s\n' \
		"$YEL" "$TIL" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$YEL" "$4" "$RESET" ;;
	# No duration, deliberately. Across this table a printed time means the gate ran and
	# passed, and tests/tooling/verify_test.sh reads it that way; the seconds a gate spent
	# discovering it could not run say nothing worth breaking that convention for.
	skip-host) printf '  %s%s%s %-12s %s%-36s%sNOT CHECKED — %s (CI still runs it)%s\n' \
		"$YEL" "$TIL" "$RESET" "$1" "$DIM" "$(gate_label "$1")" "$YEL" "$4" "$RESET" ;;
	esac
}

# 0 passed, 1 failed, 2 did not run. Called from inside a lane subshell.
run_gate() { # <gate>
	local g="$1" t0 t1 rc=0 missing="" tool mod secs

	case " $SKIP " in
	*" $g "*)
		gate_result "$g" skip-req 0 "SKIP=" || return 1
		gate_row "$g" skip-req 0 ""
		return 2
		;;
	esac

	# Off by default, on with WITH_CBMC=1. Not CBMC=1: tests/host/cbmc.sh reads
	# $CBMC as the path to the solver binary, and this environment reaches it
	# through `make cbmc`, so CBMC=1 would send it looking for a binary named 1.
	#
	# This sits ahead of the tool check on purpose: a gate that is not going to
	# run cannot fail the sweep for a missing tool, so a host with no cbmc
	# installed is not held to it.
	if [ "$g" = cbmc ] && [ -z "${WITH_CBMC:-}" ]; then
		gate_result "$g" skip-optin 0 "opt-in, WITH_CBMC=1" || return 1
		gate_row "$g" skip-optin 0 ""
		return 2
	fi

	# Ahead of the tool check for the same reason the cbmc opt-out is: a gate
	# that is not going to run cannot fail the sweep for a tool it will not use.
	if gate_unchanged "$g"; then
		gate_result "$g" skip-path 0 "unchanged" || return 1
		gate_row "$g" skip-path 0 ""
		return 2
	fi

	for tool in $(gate_need "$g"); do
		command -v "$tool" >/dev/null 2>&1 || missing="${missing:+$missing }$tool"
	done
	for mod in $(gate_need_py "$g"); do
		"$PY" -c "import $mod" >/dev/null 2>&1 || missing="${missing:+$missing }python:$mod"
	done
	if [ -n "$missing" ]; then
		# Counted apart from a SKIP= skip, and fatal at the end. Scoping a gate
		# out by hand is a decision; not having installed its tool is not, and
		# an exit status of 0 here is what lets it reach CI.
		gate_result "$g" skip-tool 0 "needs $missing" || return 1
		gate_row "$g" skip-tool 0 "needs $missing"
		return 2
	fi

	t0=$(date +%s)
	(gate_run "$g") >"$RUNDIR/$g.out" 2>&1 || rc=$?
	t1=$(date +%s)
	secs=$((t1 - t0))

	if [ "$rc" -eq 0 ]; then
		gate_result "$g" pass "$secs" "" || return 1
		gate_row "$g" pass "$secs" ""
		return 0
	fi
	# 2 from a security gate is "this host cannot answer the question", which is neither a pass
	# nor a failure, and only where no install closes the gap: there is no valgrind for
	# darwin/arm64, so `ct` returns 2 on the primary dev machine every time. Left as a pass it
	# was a green row for a gate that ran nothing, which is the exact shape of failure this
	# whole sweep exists to prevent. Loud, listed, and not fatal — unlike skip-tool, `make
	# tools-install` cannot fix it, so failing the sweep would only train people to ignore it.
	# Scoped to that family on purpose: every other gate is free to use 2 for its own meaning,
	# and docs does, for "this branch is behind origin/main".
	if [ "$rc" -eq 2 ] && gate_is_security "$g"; then
		gate_result "$g" skip-host "$secs" "not checkable on this host" || return 1
		gate_row "$g" skip-host "$secs" "not checkable on this host"
		return 2
	fi
	gate_result "$g" fail "$secs" "exit $rc" || return 1
	gate_row "$g" fail "$secs" "exit $rc"
	return 1
}

# One lane, in order. A failure stops the rest of that lane but not the others:
# the gates sharing a lane share a build directory, so running the next one over
# a half-built tree would only produce a second, confusing failure.
run_lane() { # <lane index>
	local g rc
	for g in ${LANES[$1]}; do
		rc=0
		run_gate "$g" || rc=$?
		[ "$rc" = 1 ] && return 1
	done
	return 0
}

# ---- run ------------------------------------------------------------------
# The scope caveat rides here rather than in the verdict: it is true of every
# run, so stating it once up front frames the sweep instead of nagging at the
# end of a green one.
printf '\n  %s%sopenaliro verify%s  %s%s  every host-runnable CI gate%s\n' \
	"$BOLD" "$CYAN" "$RESET" "$DIM" "$DOT" "$RESET"
printf '  %sfirmware builds (ESP-IDF / NCS) and hardware validation run separately%s\n' \
	"$DIM" "$RESET"
# Which mode this run is in, before any row prints. A filtered sweep that did
# not say it was filtered is indistinguishable from a full one that quietly
# stopped checking things.
if [ -n "$FILTER_OFF" ]; then
	printf '  %severy gate runs %s %s%s\n\n' "$DIM" "$DOT" "$FILTER_OFF" "$RESET"
else
	printf '  %sgates whose inputs this branch does not touch are skipped %s vs %s%s\n\n' \
		"$DIM" "$DOT" "$(git rev-parse --short "$FILTER_BASE" 2>/dev/null || printf '%s' "$FILTER_BASE")" "$RESET"
fi

n=${#GATES[@]}
t_all0=$(date +%s)

# Serial, fail-fast, cheapest-first: a formatting slip stops here, four seconds
# in, rather than after the parallel phase has run everything anyway.
tripwire_failed=""
for g in $TRIPWIRE; do
	rc=0
	run_gate "$g" || rc=$?
	if [ "$rc" = 1 ]; then
		tripwire_failed="$g"
		break
	fi
done

if [ -z "$tripwire_failed" ]; then
	if [ -n "${SERIAL:-}" ]; then
		# SERIAL=1: one lane at a time, stopping at the first failure. For a
		# contended machine, or for reading the output of a gate in order.
		for ((l = 0; l < ${#LANES[@]}; l++)); do
			run_lane "$l" || break
		done
	else
		printf '  %s%s %d lanes in parallel, rows appear as gates finish%s\n' \
			"$DIM" "$ARR" "${#LANES[@]}" "$RESET"
		for ((l = 0; l < ${#LANES[@]}; l++)); do
			run_lane "$l" &
		done
		wait
	fi
fi

t_all=$(($(date +%s) - t_all0))

# ---- summary --------------------------------------------------------------
# Rebuilt from the .rc files, not from what scrolled past: with lanes running at
# once the printed order is arrival order, and this table is the gate table's.
STATUS=() REASON=()
nfail=0 nskip=0 npass=0 nnotrun=0 nskip_tool=0 nskip_optin=0 nskip_path=0
for ((i = 0; i < n; i++)); do
	g="${GATES[i]}"
	if [ -f "$RUNDIR/$g.rc" ]; then
		# The per-gate time is in field 2, already printed on the gate's own
		# row as it finished; only status and reason are needed again here.
		IFS=$'\t' read -r st _ reason <"$RUNDIR/$g.rc"
	else
		st=notrun reason=""
	fi
	STATUS[i]="$st" REASON[i]="$reason"
	case "$st" in
	pass) npass=$((npass + 1)) ;;
	fail) nfail=$((nfail + 1)) ;;
	skip-req) nskip=$((nskip + 1)) ;;
	skip-optin) nskip=$((nskip + 1)) nskip_optin=$((nskip_optin + 1)) ;;
	skip-tool) nskip=$((nskip + 1)) nskip_tool=$((nskip_tool + 1)) ;;
	skip-path) nskip=$((nskip + 1)) nskip_path=$((nskip_path + 1)) ;;
	skip-host) nskip=$((nskip + 1)) ;;
	notrun) nnotrun=$((nnotrun + 1)) ;;
	esac
done

# Why a gate never started: its own lane stopped, or the tripwire did.
why_notrun() { # <gate>
	local l g st
	[ -n "$tripwire_failed" ] && { printf 'tripwire failed at %s' "$tripwire_failed"; return; }
	for ((l = 0; l < ${#LANES[@]}; l++)); do
		case " ${LANES[l]} " in *" $1 "*) ;; *) continue ;; esac
		for g in ${LANES[l]}; do
			[ -f "$RUNDIR/$g.rc" ] || continue
			IFS=$'\t' read -r st _ _ <"$RUNDIR/$g.rc"
			[ "$st" = fail ] && { printf 'its lane stopped at %s' "$g"; return; }
		done
	done
	printf 'did not start'
}

# Every gate that ran already printed its own row as it finished, so reprinting
# the whole table here would double the output to say nothing new. The one thing
# no row covers is a gate that never started: it is silently absent otherwise,
# and a gate you did not notice skipping is how a green sweep meets a red CI.
for ((i = 0; i < n; i++)); do
	[ "${STATUS[i]}" = notrun ] || continue
	printf '  %s%s %-12s %-36s%s%s\n' \
		"$DIM" "$TIL" "${GATES[i]}" "$(gate_label "${GATES[i]}")" \
		"$(why_notrun "${GATES[i]}")" "$RESET"
done
printf '  %s%s%s\n' "$DIM" "$HR" "$RESET"

# The skipped list is repeated here on purpose: a gate that did not run locally
# is the exact thing that makes a green sweep and a red CI disagree. The cbmc
# opt-out is excluded because it is the default — it gets one quiet line in the
# verdict instead, and a warning that fires every run is a warning nobody reads.
# The two skips CI agrees with: the cbmc opt-out (CI turns it back on, and says
# so in its own line) and the path filter (CI runs the same filter over the same
# diff). Everything else is this host disagreeing with CI, which is the thing
# the loud line exists to surface.
nskip_quiet=$((nskip_optin + nskip_path))

if [ "$nskip" -gt 0 ] && [ "$nskip" -ne "$nskip_quiet" ]; then
	names=""
	for ((i = 0; i < n; i++)); do
		case "${STATUS[i]}" in skip-path) continue ;; skip-*) ;; *) continue ;; esac
		names="${names:+$names, }${GATES[i]} (${REASON[i]})"
	done
	printf '  %s%s %d gate(s) SKIPPED, CI will still run them: %s%s\n\n' \
		"$YEL" "$TIL" "$((nskip - nskip_path))" "$names" "$RESET"
fi

# A normal failed gate has a durable `fail` record; only gates downstream of it
# may be absent. No recorded failure plus an absent result means bookkeeping
# broke, and accepting that state recreates the false "all 0 passed" verdict.
if [ "$nnotrun" -gt 0 ] && [ "$nfail" -eq 0 ]; then
	printf '  %s%s verify FAILED: result bookkeeping incomplete (%d gate(s) unrecorded)%s\n\n' \
		"$RED" "$CRS" "$nnotrun" "$RESET"
	exit 2
fi

if [ "$nfail" -gt 0 ]; then
	# Every failure, in gate-table order. Lanes run at once, so more than one
	# can fail in a single sweep and showing only the first would send someone
	# round the loop twice.
	# The tail, not the whole log: one gate here can emit thousands of lines of
	# its own tracing, and dumping all of it buries the failure that caused it.
	# The full file stays on disk and its path is printed, so nothing is lost.
	KEEPDIR=1
	failed_names=""
	for ((i = 0; i < n; i++)); do
		[ "${STATUS[i]}" = fail ] || continue
		g="${GATES[i]}"
		failed_names="${failed_names:+$failed_names }$g"
		nlines=$(wc -l <"$RUNDIR/$g.out")
		nlines="${nlines// /}"
		if [ "$nlines" -gt "$FAIL_TAIL" ]; then
			printf '\n  %s%s %s %s last %s of %s lines%s\n' \
				"$DIM" "$HR4" "$g" "$DOT" "$FAIL_TAIL" "$nlines" "$RESET"
		else
			printf '\n  %s%s %s%s\n' "$DIM" "$HR4" "$g" "$RESET"
		fi
		tail -n "$FAIL_TAIL" "$RUNDIR/$g.out" | sed 's/^/  /'
		printf '  %sfull log:  %s%s\n' "$DIM" "$RUNDIR/$g.out" "$RESET"
	done
	printf '\n  %s%s verify FAILED: %s%s\n' "$RED" "$CRS" "$failed_names" "$RESET"
	printf '  %sre-run one gate alone:  %sSKIP="<the others>" make verify%s\n\n' \
		"$DIM" "$BOLD" "$RESET"
	exit 1
fi

# A gate that could not run because its tool is absent is a failure of this
# sweep, not a footnote to it. Exiting 0 here is the whole bug: a pre-push hook
# sees success, the push lands, and CI runs the gate that never ran locally.
if [ "$nskip_tool" -gt 0 ]; then
	printf '  %s%s %d gate(s) COULD NOT RUN — the tool is not installed. CI will run them.%s\n' \
		"$RED" "$CRS" "$nskip_tool" "$RESET"
	printf '  %sInstall what is missing:  %smake tools-install%s\n' "$DIM" "$BOLD" "$RESET"
	printf '  %sOr accept the gap on purpose for this run:  SKIP="<gate>"%s\n\n' "$DIM" "$RESET"
	exit 1
fi

# A hand-scoped SKIP= is unusual and gets the loud line. The cbmc opt-out is the
# default, so it gets a plain one: a warning that fires on every single run is a
# warning nobody reads, and then the loud line means nothing when it matters.
if [ "$nskip" -gt 0 ] && [ "$nskip" -ne "$nskip_quiet" ]; then
	printf '  %s%s %d passed %s %d skipped %s %ds%s  %sNOT the full CI set%s\n\n' \
		"$YEL" "$CHK" "$npass" "$DOT" "$nskip" "$DOT" "$t_all" "$RESET" "$YEL" "$RESET"
elif [ "$nskip_quiet" -gt 0 ]; then
	printf '  %s%s %d passed %s %d skipped %s %ds%s\n' \
		"$GRN" "$CHK" "$npass" "$DOT" "$nskip" "$DOT" "$t_all" "$RESET"
	[ "$nskip_optin" -gt 0 ] && printf '    %scbmc did not run:  %sWITH_CBMC=1 make verify%s\n' \
		"$DIM" "$BOLD" "$RESET"
	# Named, with the escape hatch on the same line. A gate that skipped itself
	# is only trustworthy if the reader can see how many did and undo it in one
	# step; silence here would be the filter quietly becoming the default nobody
	# audits.
	[ "$nskip_path" -gt 0 ] && printf '    %s%d gate(s) skipped: nothing they read changed.  %sFILTER=0 make verify%s%s sweeps everything.%s\n' \
		"$DIM" "$nskip_path" "$BOLD" "$RESET" "$DIM" "$RESET"
	printf '\n'
else
	printf '  %s%s all %d host-runnable CI gates passed %s %ds%s\n\n' \
		"$GRN" "$CHK" "$npass" "$DOT" "$t_all" "$RESET"
fi
