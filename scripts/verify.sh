#!/usr/bin/env bash
#
# Pre-push sweep: every CI gate that a host can run, in one shot.
#
# The point of this script is that "it passed locally" and "it will pass CI"
# mean the same thing. Each row below is one CI *job* (not one workflow —
# tooling.yml and workflow-lint.yml each contribute several), running the same
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
# It still gets a summary row saying it did not run. cbmc.yml has no path
# filter, so the PR runs it whatever happened here; a gate that quietly
# disappears from the sweep is the exact failure this script exists to prevent.
#
# A gate whose tool is missing FAILS the sweep. It says so on its row, it is
# counted apart from a hand-scoped SKIP=, and the run exits nonzero. Anything
# softer is the original bug wearing a warning label: CI runs that gate whatever
# this host has installed, so "could not check" has to read as "not verified",
# not as "fine". `make tools-install` is the fix; SKIP="<gate>" is the override
# for someone who has decided to accept the gap.
#
# Env:
#   WITH_CBMC=1        also run the cbmc proof (off by default, see above)
#   SERIAL=1           one gate at a time, fail-fast, instead of lanes
#   SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
#   COV_MIN=90         line-coverage floor, matching host-tests.yml
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
GATES=(
	test-web    # 0s   twin-web.yml : drift-gate
	actionlint  # 0s   workflow-lint.yml : actionlint
	zizmor      # 0s   workflow-lint.yml : zizmor
	format      # 1s   format.yml
	shellcheck  # 1s   tooling.yml : shellcheck
	licenses    # 3s   tooling.yml : licenses
	fuzz        # 3s   fuzz.yml
	clang-tidy  # 4s   clang-tidy.yml
	test        # 5s   host-tests.yml : host
	twin-wasm   # 7s   twin-web.yml : wasm-firmware  (2s warm, 7s cold)
	test-tui    # 9s   release.yml : tui
	test-san    # 8s   sanitizers.yml
	patch-drift # 11s  patch-drift.yml
	docs        # 12s  docs.yml
	test-port   # 14s  port-tests.yml
	test-ws     # 14s  tooling.yml : ws-seed
	test-verify # 15s  tooling.yml : verify-tests
	coverage    # 18s  host-tests.yml : coverage (+ the line floor)
	cbmc        # 82s  cbmc.yml
)

# ---- lanes ----------------------------------------------------------------
# Run serially the table above is 103s of work on a machine with eight cores,
# which is mostly idle waiting. Lanes run at once; gates inside a lane run in
# order. Two rules set the shape:
#
#   1. Gates that write the same path must share a lane. Exactly one pair does:
#      `test` and `test-san` are the same run.sh writing the same
#      build/host_test* binaries, so side by side they would overwrite each
#      other's build with no error at all. twin-wasm and docs share one for a
#      softer reason: docs renders the twin, so it should see the rebuilt
#      twin.js, which is the order the old serial sweep happened to have.
#   2. Everything else is already hermetic and was checked one at a time:
#      test-ws and patch-drift build under `mktemp -d`, test-port under
#      `mktemp -t` per binary, coverage under build/coverage/, fuzz under
#      build/fuzz/, cbmc writes only its own logs. The lint gates only read.
#
# TRIPWIRE runs first and serially, and a failure there stops the sweep. It is
# the whole sub-2s set, so a formatting slip costs four seconds instead of the
# full run: the fail-fast the parallel phase gives up, bought back where it is
# cheap. It also runs the two whole-tree scanners (licenses, format) before
# anything starts writing, so neither reads a file mid-rewrite.
TRIPWIRE="test-web actionlint zizmor format shellcheck licenses"
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
	"test-ws patch-drift"      # 23s
	"twin-wasm docs clang-tidy" # 19s  rebuild the twin before docs renders it
	"test-port fuzz"           # 17s
	"test-tui"                 # 9s   bun only, shares nothing with the C gates
	"test test-san"            # 15s  same run.sh, same build/host_test* paths
	"test-verify"              # 15s  13 stub sweeps back to back, under the floor
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
	*) echo "" ;;
	esac
}

# Python packages a gate's suites import. `command -v` cannot see these: they
# are modules inside an interpreter, not binaries on PATH, which is exactly how
# they went unnoticed. Absent, the suites still run and still report success,
# having quietly skipped the checks that need them — host-tests.yml installs
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
	test-web) echo "twin constants match the firmware" ;;
	actionlint) echo "workflow syntax" ;;
	fuzz) echo "wire-parser fuzz corpus" ;;
	test) echo "host KAT suite" ;;
	twin-wasm) echo "rebuild WASM twin + node selftest" ;;
	patch-drift) echo "vendored patches still apply" ;;
	docs) echo "site builds, no dead links" ;;
	test-san) echo "host suite under ASan + UBSan" ;;
	test-port) echo "ESP32 port tests" ;;
	test-ws) echo "workspace auto-seeding" ;;
	test-tui) echo "guided bench types, tests, build" ;;
	test-verify) echo "this sweep's own tests" ;;
	coverage) echo "line coverage >= ${COV_MIN}%" ;;
	clang-tidy) echo "static analysis of the core" ;;
	zizmor) echo "workflow security audit" ;;
	licenses) echo "licence store is consistent" ;;
	cbmc) echo "wire-parser memory-safety proof" ;;
	esac
}

# The command each gate runs. Where CI runs a make target, so do we; where CI
# runs a raw command, this reproduces it verbatim.
gate_run() {
	case "$1" in
	format)
		git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' \
			| xargs clang-format --dry-run --Werror
		;;
	shellcheck) git ls-files '*.sh' | xargs shellcheck -S warning ;;
	test-web) make --no-print-directory test-web ;;
	actionlint) actionlint -color ;;
	fuzz) make --no-print-directory fuzz ;;
	test) make --no-print-directory test ;;
	twin-wasm)
		# twin-web.yml runs the node selftest against the committed twin.js,
		# rebuilds, and runs it again. The rebuild diff is warn-only there
		# (emsdk binaries differ across host OSes), so it is warn-only here.
		node web-twin/selftest.cjs \
			&& make --no-print-directory twin-wasm \
			&& node web-twin/selftest.cjs \
			&& { git diff --exit-code --stat -- web-twin/twin.js \
				|| printf '  note: twin.js differs from this rebuild (warn-only, as in CI)\n'; }
		;;
	patch-drift) tests/tooling/patch_drift_check.sh ;;
	docs) make --no-print-directory docs ;;
	test-san) make --no-print-directory test-san ;;
	# Host layers only. Its third layer, verify_port.sh, shells out to `idf.py
	# build` whenever ESP-IDF is sourced -- which port-tests.yml's runner never
	# is, so CI does not run it either. Left alone it would drop a multi-minute
	# firmware build into a 33s sweep, from a shell state the sweep cannot see.
	test-port) WOZ_NO_TARGET_BUILD=1 make --no-print-directory test-port ;;
	test-ws) make --no-print-directory test-ws ;;
	# All three steps release.yml runs, in its order. `make tui-test` alone would
	# pass a branch whose types are broken or whose executable does not link,
	# because CI only finds those in the typecheck and release steps.
	test-tui) (cd "$ROOT/tools/tui" && bun run typecheck && bun run test && bun run release) ;;
	test-verify) make --no-print-directory test-verify ;;
	coverage)
		# host-tests.yml runs `make coverage` and THEN enforces the floor as a
		# separate step. Running coverage alone would pass where CI fails.
		make --no-print-directory coverage || return 1
		COV_MIN="$COV_MIN" python3 -c '
import json, os, sys
t = json.load(open("build/coverage/summary.json"))["data"][0]["totals"]
pct, floor = t["lines"]["percent"], float(os.environ["COV_MIN"])
print(f"  total line coverage {pct:.2f}% (floor {floor:.0f}%)")
sys.exit(0 if pct >= floor else 1)'
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
		# tooling.yml gates the licence *store*, not full REUSE compliance:
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
	skip-tool) printf '  %s%s%s %-12s %s%-36s%sSKIPPED — %s (CI still runs it)%s\n' \
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
printf '  %sfirmware builds (ESP-IDF / NCS) and hardware validation run separately%s\n\n' \
	"$DIM" "$RESET"

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
nfail=0 nskip=0 npass=0 nnotrun=0 nskip_tool=0 nskip_optin=0
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
if [ "$nskip" -gt 0 ] && [ "$nskip" -ne "$nskip_optin" ]; then
	names=""
	for ((i = 0; i < n; i++)); do
		case "${STATUS[i]}" in skip-*) ;; *) continue ;; esac
		names="${names:+$names, }${GATES[i]} (${REASON[i]})"
	done
	printf '  %s%s %d gate(s) SKIPPED, CI will still run them: %s%s\n\n' \
		"$YEL" "$TIL" "$nskip" "$names" "$RESET"
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
if [ "$nskip" -gt 0 ] && [ "$nskip" -ne "$nskip_optin" ]; then
	printf '  %s%s %d passed %s %d skipped %s %ds%s  %sNOT the full CI set%s\n\n' \
		"$YEL" "$CHK" "$npass" "$DOT" "$nskip" "$DOT" "$t_all" "$RESET" "$YEL" "$RESET"
elif [ "$nskip_optin" -gt 0 ]; then
	printf '  %s%s %d passed %s %d skipped %s %ds%s\n' \
		"$GRN" "$CHK" "$npass" "$DOT" "$nskip" "$DOT" "$t_all" "$RESET"
	printf '    %scbmc did not run:  %sWITH_CBMC=1 make verify%s\n\n' \
		"$DIM" "$BOLD" "$RESET"
else
	printf '  %s%s all %d host-runnable CI gates passed %s %ds%s\n\n' \
		"$GRN" "$CHK" "$npass" "$DOT" "$t_all" "$RESET"
fi
