#!/usr/bin/env bash
#
# Build + run the wire-parser fuzz targets. Plain C against the same seam as the
# host suite (tests/host/sources.sh); no NCS toolchain or hardware.
#
# Two build modes, auto-selected:
#   libfuzzer  — clang with a libFuzzer runtime: coverage-guided fuzzing that
#                writes new inputs back into the corpus. Used in CI (Linux clang).
#   standalone — any clang (Apple clang ships no libFuzzer): links a portable
#                main() that replays the checked-in corpus, built under ASan +
#                UBSan. Turns the corpus into an everywhere-runnable regression.
#
# Env:
#   CC=…               compiler (default cc)
#   FUZZ_SECONDS=N     libfuzzer wall-clock budget per target (default 20)
#   FUZZ_STANDALONE=1  force standalone replay even if libFuzzer is present
# Args: target names to restrict the run (default: all).
set -euo pipefail
shopt -s nullglob

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

CC="${CC:-cc}"
FZ="$ROOT/tests/host/fuzz"
OUT="$ROOT/build/fuzz"
APDU_SRC="$ROOT/modules/woz_aliro/src"
SECONDS_BUDGET="${FUZZ_SECONDS:-20}"

# target -> parser source under test (harness stays minimal: one parser each).
# A case function rather than an associative array so this runs on the stock
# macOS bash 3.2 as well as CI's bash.
targets=(aliro_uwb_msg ccc_mac aliro_apdu stepup aliro_assert)
target_src() {
	case "$1" in
	aliro_uwb_msg) echo "$SRC/aliro/aliro_uwb_msg_parser.c" ;;
	ccc_mac) echo "$SRC/ccc/ccc_mac.c" ;;
	aliro_apdu) echo "$APDU_SRC/aliro_apdu.c" ;;
	stepup) echo "$APDU_SRC/aliro_stepup_parse.c" ;;
	aliro_assert) echo "$APDU_SRC/aliro_assert.c" ;;
	*) return 1 ;;
	esac
}
# Extra sources a target links beyond its parser (dependencies compiled in).
target_extra() {
	case "$1" in
	aliro_assert) echo "$APDU_SRC/aliro_hash.c" ;; # HMAC/SHA the verifier calls
	*) echo "" ;;
	esac
}

# aliro_apdu.h / aliro_stepup.h live in the woz_aliro module, off the shared
# INCS path (src for the codec headers, include for the public API).
EXTRA_INCS=(-I"$APDU_SRC" -I"$ROOT/modules/woz_aliro/include")

[ "$#" -gt 0 ] && targets=("$@")

# Detect a usable libFuzzer runtime unless standalone is forced.
mode=standalone
if [ -z "${FUZZ_STANDALONE:-}" ]; then
	probe="$OUT/.probe"
	mkdir -p "$OUT"
	printf 'int LLVMFuzzerTestOneInput(const unsigned char*d,unsigned long n){(void)d;(void)n;return 0;}\n' \
		| "$CC" -x c -fsanitize=fuzzer -o "$probe" - >/dev/null 2>&1 && mode=libfuzzer
	rm -f "$probe"
fi

mkdir -p "$OUT"
printf '  fuzz mode: %s  (CC=%s)\n' "$mode" "$CC"

fail=0
for name in "${targets[@]}"; do
	if ! src="$(target_src "$name")"; then
		printf '  unknown target: %s\n' "$name" >&2
		fail=1
		continue
	fi
	corpus="$FZ/corpus/$name"
	mkdir -p "$corpus"
	bin="$OUT/$name"
	# shellcheck disable=SC2086  # extra is a deliberate word-split source list
	extra="$(target_extra "$name")"

	if [ "$mode" = libfuzzer ]; then
		# shellcheck disable=SC2086
		"$CC" -std=c11 -w -g -O1 -fsanitize=fuzzer,address,undefined \
			-fno-sanitize-recover=all "${DEFS[@]}" "${INCS[@]}" "${EXTRA_INCS[@]}" \
			"$FZ/fuzz_$name.c" "$src" $extra -o "$bin"
		printf '  [%s] fuzzing %ss…\n' "$name" "$SECONDS_BUDGET"
		"$bin" -max_total_time="$SECONDS_BUDGET" -timeout=10 -print_final_stats=1 \
			"$corpus" || fail=1
	else
		# shellcheck disable=SC2086
		"$CC" -std=c11 -w -g -O1 -fsanitize=address,undefined \
			-fno-sanitize-recover=all "${DEFS[@]}" "${INCS[@]}" "${EXTRA_INCS[@]}" \
			"$FZ/fuzz_$name.c" "$src" $extra "$FZ/standalone_main.c" -o "$bin"
		files=("$corpus"/*)
		printf '  [%s] replaying %d corpus input(s)…\n' "$name" "${#files[@]}"
		if [ "${#files[@]}" -gt 0 ]; then
			"$bin" "${files[@]}" || fail=1
		else
			"$bin" || fail=1
		fi
	fi
done

if [ "$fail" -ne 0 ]; then
	printf '\n  RESULT: FAIL\n'
	exit 1
fi
printf '\n  RESULT: PASS\n'
