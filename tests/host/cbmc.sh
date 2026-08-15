#!/usr/bin/env bash
#
# Bounded model-check the wire parsers with CBMC: prove memory safety (no
# out-of-bounds access, no bad pointer, no overflowing conversion) for ALL
# inputs up to each harness bound. Reuses the host seam's include/define lists
# (sources.sh).
#
# Env: CBMC=… (default cbmc). Args: harness names to restrict the run.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"
. "$ROOT/scripts/lib/ui.sh"

CBMC="${CBMC:-cbmc}"
CB="$ROOT/tests/host/cbmc"
SRCD="$ROOT/modules/ultrawidelock_uwb/src"
APDU_SRC="$ROOT/modules/ultrawidelock_cred/src"
EXTRA_INCS=(-I"$APDU_SRC")

COMMON=(
	--function harness
	--bounds-check
	--pointer-check
	--conversion-check
	--div-by-zero-check
	--unwinding-assertions
	--drop-unused-functions
)

targets=(ultrawidelock_uwb_msg ccc_mac ultrawidelock_apdu)
[ "$#" -gt 0 ] && targets=("$@")

# harness -> parser source under proof.
harness_src() {
	case "$1" in
	ultrawidelock_uwb_msg) echo "$SRCD/cred/ultrawidelock_uwb_msg_parser.c" ;;
	ccc_mac) echo "$SRCD/ccc/ccc_mac.c" ;;
	ultrawidelock_apdu) echo "$APDU_SRC/ultrawidelock_apdu.c" ;;
	*) return 1 ;;
	esac
}

# Loop-unwind bound: enough to fully unwind every loop so --unwinding-assertions
# proves completeness (read_be reads <= 8 bytes; TLV walks advance >= 2 bytes).
harness_unwind() {
	case "$1" in
	ultrawidelock_uwb_msg) echo 12 ;;
	ccc_mac) echo 12 ;;
	ultrawidelock_apdu) echo 28 ;;
	*) return 1 ;;
	esac
}

# One harness. Solving is minutes of silence per proof, so each is a step of
# scripts/lib/ui.sh: the elapsed clock under the bar is the only sign CBMC is
# working rather than wedged.
prove() { # <name> <src> <unwind> <log>
	local name="$1" src="$2" uw="$3" log="$4"
	printf '  [cbmc] %s (unwind %s)…\n' "$name" "$uw"
	# Full per-property dump goes to a log; the terminal keeps one line per
	# harness (plus the proof-size summary). The log is replayed on failure.
	if "$CBMC" "${COMMON[@]}" --unwind "$uw" "${DEFS[@]}" "${INCS[@]}" "${EXTRA_INCS[@]}" \
		"$CB/cbmc_$name.c" "$src" >"$log" 2>&1; then
		printf '  [cbmc] %s: SUCCESSFUL (%s)\n' "$name" \
			"$(sed -n 's/^\*\* \(0 of [0-9]* failed\).*/\1/p' "$log")"
	else
		cat "$log"
		printf '  [cbmc] %s: FAILED (full log: %s)\n' "$name" "$log"
		return 1
	fi
}

ui_begin "cbmc proofs" "${targets[@]}"
fail=0
for name in "${targets[@]}"; do
	if ! src="$(harness_src "$name")" || ! uw="$(harness_unwind "$name")"; then
		ui_clear
		printf '  unknown harness: %s\n' "$name" >&2
		fail=1
		continue
	fi
	log="${ULTRAWIDELOCK_BUILD_ROOT:-$ROOT/build}/host/cbmc/$name.log"
	mkdir -p "$(dirname "$log")"
	# ui_run_try, not ui_run: every harness gets its verdict, and the RESULT
	# line below is the one that decides the exit status.
	ui_run_try "$name" prove "$name" "$src" "$uw" "$log" || fail=1
done

# The footer first, then RESULT: the verdict line is what CI greps for and it
# stays the last thing on stdout.
if [ "$fail" -ne 0 ]; then
	ui_end 1 || true
	printf '\n  RESULT: FAIL\n'
	exit 1
fi
ui_end
printf '\n  RESULT: PASS\n'
