#!/usr/bin/env bash
#
# Bounded model-check the wire parsers with CBMC: prove memory safety (no
# out-of-bounds access, no bad pointer, no overflowing conversion) for ALL
# inputs up to each harness bound — the exhaustive counterpart to the fuzz
# corpus replay. Reuses the host seam's include/define lists (sources.sh).
#
# Env: CBMC=… (default cbmc). Args: harness names to restrict the run.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

CBMC="${CBMC:-cbmc}"
CB="$ROOT/tests/host/cbmc"
SRCD="$ROOT/modules/woz_uwb/src"
APDU_SRC="$ROOT/modules/woz_aliro/src"
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

targets=(aliro_uwb_msg ccc_mac aliro_apdu)
[ "$#" -gt 0 ] && targets=("$@")

# harness -> parser source under proof.
harness_src() {
	case "$1" in
	aliro_uwb_msg) echo "$SRCD/aliro/aliro_uwb_msg_parser.c" ;;
	ccc_mac) echo "$SRCD/ccc/ccc_mac.c" ;;
	aliro_apdu) echo "$APDU_SRC/aliro_apdu.c" ;;
	*) return 1 ;;
	esac
}

# Loop-unwind bound: enough to fully unwind every loop so --unwinding-assertions
# proves completeness (read_be reads <= 8 bytes; TLV walks advance >= 2 bytes).
harness_unwind() {
	case "$1" in
	aliro_uwb_msg) echo 12 ;;
	ccc_mac) echo 12 ;;
	aliro_apdu) echo 28 ;;
	*) return 1 ;;
	esac
}

fail=0
for name in "${targets[@]}"; do
	if ! src="$(harness_src "$name")" || ! uw="$(harness_unwind "$name")"; then
		printf '  unknown harness: %s\n' "$name" >&2
		fail=1
		continue
	fi
	printf '  [cbmc] %s (unwind %s)…\n' "$name" "$uw"
	# Full per-property dump goes to a log; the terminal keeps one line per
	# harness (plus the proof-size summary). The log is replayed on failure.
	log="$ROOT/build/cbmc_$name.log"
	mkdir -p "$ROOT/build"
	if "$CBMC" "${COMMON[@]}" --unwind "$uw" "${DEFS[@]}" "${INCS[@]}" "${EXTRA_INCS[@]}" \
		"$CB/cbmc_$name.c" "$src" >"$log" 2>&1; then
		printf '  [cbmc] %s: SUCCESSFUL (%s)\n' "$name" \
			"$(sed -n 's/^\*\* \(0 of [0-9]* failed\).*/\1/p' "$log")"
	else
		cat "$log"
		printf '  [cbmc] %s: FAILED (full log: build/cbmc_%s.log)\n' "$name" "$name"
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	printf '\n  RESULT: FAIL\n'
	exit 1
fi
printf '\n  RESULT: PASS\n'
