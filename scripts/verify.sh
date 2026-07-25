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
# Gates are ordered cheapest-first and the run stops at the first failure, so a
# one-second formatting slip never costs the eighty-six seconds of cbmc.
#
# A gate whose tool is missing is SKIPPED LOUDLY: it says so on its row, it is
# counted in the summary, and the final line names it. It never silently passes,
# because CI will still run it.
#
# Env:
#   SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
#   COV_MIN=90         line-coverage floor, matching host-tests.yml
#   NO_COLOR=1         plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

COV_MIN="${COV_MIN:-90}"
SKIP="${SKIP:-}"

# ---- glyphs + colour (same vocabulary as scripts/test-runner.sh) -----------
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' CYAN=$'\033[36m' GRN=$'\033[32m'
	RED=$'\033[31m' YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" ARR="▸" TIL="~" DOT="•"
else
	BOLD="" DIM="" CYAN="" GRN="" RED="" YEL="" RESET=""
	CHK="+" CRS="x" ARR=">" TIL="~" DOT="*"
fi

# A running gate prints a "▸ name label" line that its result overwrites. That
# only works on a terminal: piped to a file or a hook, CR and erase-to-EOL would
# just be noise, so there the row is printed once, when it is done.
if [[ -t 1 ]]; then ISTTY=1 CR=$'\r' EL=$'\033[K'; else ISTTY=0 CR="" EL=""; fi

# ---- gate table -----------------------------------------------------------
# Cheapest-first, by wall time measured on an 8-core Apple silicon host with
# warm caches. A gate whose tool is absent costs nothing, so position is set by
# what it costs when the tool IS there. cbmc is last because it is 86s of the
# ~170s total; everything above it together is under ninety seconds.
HR="--------------------------------------------"
GATES=(
	test-web    # 0s   twin-web.yml : drift-gate
	actionlint  # 0s   workflow-lint.yml : actionlint
	format      # 1s   format.yml
	shellcheck  # 1s   tooling.yml : shellcheck
	clang-tidy  # 4s   clang-tidy.yml
	fuzz        # 4s   fuzz.yml
	test        # 5s   host-tests.yml : host
	twin-wasm   # 7s   twin-web.yml : wasm-firmware
	patch-drift # 8s   patch-drift.yml
	docs        # 9s   docs.yml
	test-san    # 9s   sanitizers.yml
	test-port   # 14s  port-tests.yml
	test-ws     # 14s  tooling.yml : hermetic
	coverage    # 20s  host-tests.yml : coverage (+ the line floor)
	zizmor      # ?    workflow-lint.yml : zizmor  (unmeasured, not installed here)
	licenses    # ?    tooling.yml : licenses      (unmeasured, not installed here)
	cbmc        # 86s  cbmc.yml
)

# What each gate needs on PATH. Empty = nothing beyond a shell and a compiler.
# A bash-3.2 case function, not an associative array: macOS ships bash 3.2 and
# tests/host/fuzz.sh already sets this precedent.
gate_need() {
	case "$1" in
	format) echo "clang-format" ;;
	shellcheck) echo "shellcheck" ;;
	test-web) echo "python3" ;;
	actionlint) echo "actionlint" ;;
	twin-wasm) echo "node" ;; # emcc is resolved from ~/emsdk by twin-wasm.sh
	docs) echo "doxygen dot" ;;
	coverage) echo "python3" ;;
	clang-tidy) echo "clang-tidy" ;;
	zizmor) echo "zizmor" ;;
	licenses) echo "reuse" ;;
	cbmc) echo "cbmc" ;;
	*) echo "" ;;
	esac
}

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
	test-port) make --no-print-directory test-port ;;
	test-ws) make --no-print-directory test-ws ;;
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
		# shellcheck disable=SC1091  # repo-local, sourced for its arrays
		. tests/host/sources.sh
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

# ---- run ------------------------------------------------------------------
printf '\n  %s%sopenaliro verify%s  %s%s  every host-runnable CI gate%s\n\n' \
	"$BOLD" "$CYAN" "$RESET" "$DIM" "$DOT" "$RESET"

n=${#GATES[@]}
STATUS=() TIMES=() REASON=()
failed_gate="" nfail=0 nskip=0 npass=0 t_all0=$(date +%s)

for ((i = 0; i < n; i++)); do
	g="${GATES[i]}"
	STATUS[i]="notrun" TIMES[i]=0 REASON[i]=""

	# explicitly scoped out for this run
	case " $SKIP " in
	*" $g "*)
		STATUS[i]="skip" REASON[i]="SKIP=" nskip=$((nskip + 1))
		printf '  %s%s%s %-12s %s%-36s%sskipped via SKIP=%s\n' \
			"$YEL" "$TIL" "$RESET" "$g" "$DIM" "$(gate_label "$g")" "$YEL" "$RESET"
		continue
		;;
	esac

	# tool availability
	missing=""
	for tool in $(gate_need "$g"); do
		command -v "$tool" >/dev/null 2>&1 || missing="${missing:+$missing }$tool"
	done
	if [ -n "$missing" ]; then
		STATUS[i]="skip" REASON[i]="needs $missing" nskip=$((nskip + 1))
		printf '  %s%s%s %-12s %s%-36s%sSKIPPED — needs %s (CI still runs it)%s\n' \
			"$YEL" "$TIL" "$RESET" "$g" "$DIM" "$(gate_label "$g")" "$YEL" "$missing" "$RESET"
		continue
	fi

	[ "$ISTTY" = 1 ] && printf '  %s%s%s %-12s %s%s%s' \
		"$CYAN" "$ARR" "$RESET" "$g" "$DIM" "$(gate_label "$g")" "$RESET"
	out="$(mktemp -t oa-verify.XXXXXX)"
	t0=$(date +%s)
	rc=0
	(gate_run "$g") >"$out" 2>&1 || rc=$?
	t1=$(date +%s)
	TIMES[i]=$((t1 - t0))

	if [ "$rc" -eq 0 ]; then
		STATUS[i]="pass" npass=$((npass + 1))
		printf '%s  %s%s%s %-12s %s%-36s%4ds%s%s\n' "$CR" \
			"$GRN" "$CHK" "$RESET" "$g" "$DIM" "$(gate_label "$g")" "${TIMES[i]}" "$RESET" "$EL"
		rm -f "$out"
	else
		STATUS[i]="fail" REASON[i]="exit $rc" nfail=$((nfail + 1)) failed_gate="$g"
		printf '%s  %s%s%s %-12s %s%-36s%4ds  FAILED (exit %d)%s%s\n' "$CR" \
			"$RED" "$CRS" "$RESET" "$g" "$RED" "$(gate_label "$g")" "${TIMES[i]}" "$rc" "$RESET" "$EL"
		printf '\n%s---- %s output ----------------------%s\n' "$DIM" "$g" "$RESET"
		cat "$out"
		printf '%s%s%s\n\n' "$DIM" "$HR" "$RESET"
		rm -f "$out"
		break # fail fast: the cheap gates ran first for exactly this reason
	fi
done

t_all=$(($(date +%s) - t_all0))

# ---- summary --------------------------------------------------------------
printf '\n  %sGate           Status     Time%s\n' "$BOLD" "$RESET"
printf '  %s%s%s\n' "$DIM" "$HR" "$RESET"
for ((i = 0; i < n; i++)); do
	g="${GATES[i]}"
	case "${STATUS[i]}" in
	pass) printf '  %s%s%s %-12s %spassed%s %6ds\n' "$GRN" "$CHK" "$RESET" "$g" "$GRN" "$RESET" "${TIMES[i]}" ;;
	fail) printf '  %s%s%s %-12s %sFAILED%s %6ds\n' "$RED" "$CRS" "$RESET" "$g" "$RED" "$RESET" "${TIMES[i]}" ;;
	skip) printf '  %s%s%s %-12s %sskipped%s     %s— %s%s\n' "$YEL" "$TIL" "$RESET" "$g" "$YEL" "$RESET" "$DIM" "${REASON[i]}" "$RESET" ;;
	*) printf '    %-12s %snot run%s     %s— stopped at %s%s\n' "$g" "$DIM" "$RESET" "$DIM" "$failed_gate" "$RESET" ;;
	esac
done
printf '  %s%s%s\n' "$DIM" "$HR" "$RESET"
printf '  %s%d gates %s %d passed %s %d failed %s %d skipped %s %ds total%s\n\n' \
	"$DIM" "$n" "$DOT" "$npass" "$DOT" "$nfail" "$DOT" "$nskip" "$DOT" "$t_all" "$RESET"

# The skipped list is repeated here on purpose: a gate that did not run locally
# is the exact thing that makes a green sweep and a red CI disagree.
if [ "$nskip" -gt 0 ]; then
	names=""
	for ((i = 0; i < n; i++)); do
		[ "${STATUS[i]}" = skip ] || continue
		names="${names:+$names, }${GATES[i]} (${REASON[i]})"
	done
	printf '  %s%s %d gate(s) SKIPPED, CI will still run them: %s%s\n\n' \
		"$YEL" "$TIL" "$nskip" "$names" "$RESET"
fi

if [ "$nfail" -gt 0 ]; then
	printf '  %s%s verify FAILED at %s — fix it and re-run.%s\n' "$RED" "$CRS" "$failed_gate" "$RESET"
	printf '  %sRe-run just the rest with SKIP="%s"%s\n\n' "$DIM" "$failed_gate" "$RESET"
	exit 1
fi

if [ "$nskip" -gt 0 ]; then
	printf '  %s%s %d gates passed, %d skipped — NOT the full CI set.%s\n\n' \
		"$YEL" "$CHK" "$npass" "$nskip" "$RESET"
else
	printf '  %s%s all %d host-runnable CI gates passed.%s\n' "$GRN" "$CHK" "$npass" "$RESET"
	printf '  %sFirmware builds (ESP-IDF / NCS) and hardware validation run separately.%s\n\n' \
		"$DIM" "$RESET"
fi
