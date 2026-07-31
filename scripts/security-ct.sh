#!/usr/bin/env bash
#
# security-ct.sh — secret-dependent branches and table lookups in the CCC key ladder.
#
# Every other gate in this repo asks whether the code computes the right answer. This one asks
# whether it takes the same amount of time doing it, which no test, no sanitizer and no fuzzer in
# the tree can see: a KDF that early-outs on a key byte passes every existing check with a green
# tick, and hands an attacker the key one byte at a time.
#
# The mechanism is ctgrind's, and it is almost free. Memcheck already reports a branch or an
# array index that depends on undefined memory. Poison the URSK instead of leaving it
# uninitialised and that same report becomes "this branched on the key". Nothing new is
# instrumented; the harness (tests/host/ct/ct_main.c) just marks the secret and runs the ladder.
#
# Scope, stated up front because a green run means nothing without it: the AES primitive is
# suppressed. tests/host/aes_ref.c is an S-box implementation and is variable-time by
# construction, and it is not the primitive that ships — nRF5340 uses CryptoCell through PSA,
# ESP32 uses mbedTLS over the AES peripheral. So this gate covers the ladder and the SP0 wrapper,
# which is the code this project wrote, and says nothing about the cipher underneath, which it
# did not. tests/host/ct/host-aes.supp is where that boundary is drawn.
#
#   scripts/security-ct.sh          # build + run under memcheck
#   CT_DOCKER=1 scripts/security-ct.sh
#   make security-ct
#
# On Apple silicon there is no valgrind, and there will not be one. That is a real hole in the
# pre-push sweep rather than something to paper over, so the script says so on stdout, exits 2
# (distinct from a finding's 1), and offers CT_DOCKER=1 to run the identical command inside the
# linux/amd64 image CI uses. verify.sh turns the 2 into a row that reads "not run here, runs in
# CI", the same shape cbmc already has.
#
# Env:
#   CT_DOCKER=1     run inside docker (linux/amd64) instead of natively
#   CT_CC=clang     compiler (default: cc)
#   NO_COLOR=1      plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' GRN=$'\033[32m'
	YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" WRN="!"
else
	BOLD="" DIM="" RED="" GRN="" YEL="" RESET=""
	CHK="+" CRS="x" WRN="!"
fi

have() { command -v "$1" >/dev/null 2>&1; }
printf '\n%s── ct · secret-dependent control flow%s\n' "$BOLD" "$RESET"

# ---- docker escape hatch ---------------------------------------------------
if [ -n "${CT_DOCKER:-}" ]; then
	have docker || {
		printf '  %s%s%s CT_DOCKER=1 but docker is not installed\n' "$RED" "$CRS" "$RESET"
		exit 2
	}
	printf '  %srunning in docker (linux/amd64), the same command CI runs%s\n' "$DIM" "$RESET"
	exec docker run --rm --platform linux/amd64 -v "$ROOT:/src" -w /src \
		-e NO_COLOR="${NO_COLOR:-}" ubuntu:24.04 \
		bash -c 'apt-get update -qq && apt-get install -y -qq clang valgrind >/dev/null \
			&& CT_CC=clang scripts/security-ct.sh'
fi

# ---- host capability -------------------------------------------------------
if ! have valgrind; then
	printf '  %s%s%s valgrind is not available on this host.\n' "$YEL" "$WRN" "$RESET"
	printf '      There is no valgrind for darwin/arm64, so this is a hole in the local sweep\n'
	printf '      rather than a missing install. CI runs the gate on every push regardless.\n'
	printf '      To reproduce a CI failure here: CT_DOCKER=1 scripts/security-ct.sh\n\n'
	exit 2
fi

CC="${CT_CC:-cc}"
have "$CC" || {
	printf '  %s%s%s no C compiler (%s) on PATH\n' "$RED" "$CRS" "$RESET" "$CC"
	exit 1
}

# valgrind/memcheck.h ships with valgrind; without the header the harness silently compiles to a
# no-op and the run reports a pass having poisoned nothing. That is the failure mode this whole
# repository's gate philosophy is built to refuse, so it is checked before building.
if ! printf '#include <valgrind/memcheck.h>\nint main(void){return 0;}\n' \
	| "$CC" -x c - -o /dev/null 2>/dev/null; then
	printf '  %s%s%s valgrind/memcheck.h not found — the harness would build without poisoning\n' \
		"$RED" "$CRS" "$RESET"
	printf '      and report a pass having checked nothing. Install the valgrind headers\n'
	printf '      (apt: valgrind; brew: valgrind on x86_64 only).\n'
	exit 1
fi

# ---- build -----------------------------------------------------------------
# The same source list the host tests use, so the ladder compiled here is byte-for-byte the one
# `make test` covers. -O1: -O0 inlines nothing and hides branches the optimiser would have kept,
# -O2 can turn a branch into a cmov and report clean on code that is not.
# shellcheck disable=SC1091
. "$ROOT/tests/host/sources.sh"

OUT="$ROOT/build/ct"
mkdir -p "$OUT"

CT_SRCS=(
	"$ROOT/modules/woz_uwb/src/ccc/ccc_kdf.c"
	"$ROOT/modules/woz_uwb/src/ccc/ccc_sts.c"
	"$ROOT/modules/woz_uwb/src/ccc/ccc_mac.c"
	"$ROOT/tests/host/aes_ref.c"
	"$ROOT/tests/host/ct/ct_main.c"
)

printf '  %sscope: %d source file(s), -O1, CT_POISON on%s\n' "$DIM" "${#CT_SRCS[@]}" "$RESET"

if ! "$CC" -std=c11 -O1 -g -fno-omit-frame-pointer -DCT_POISON \
	"${DEFS[@]}" "${INCS[@]}" -I"$ROOT/tests/host/shim" \
	"${CT_SRCS[@]}" "${SHIM_SRCS[@]}" -o "$OUT/ct" 2>"$OUT/build.log"; then
	printf '  %s%s%s the harness did not build\n' "$RED" "$CRS" "$RESET"
	sed 's/^/      /' "$OUT/build.log" | head -30
	exit 1
fi

# ---- run -------------------------------------------------------------------
# --error-exitcode makes a finding fail the gate; without it valgrind reports and exits 0.
# --track-origins turns "branch on undefined data" into "branch on data that came from ursk",
# which is the difference between a finding someone can act on and one they will dismiss.
# --partial-loads-ok=no: a word-at-a-time load that straddles the end of a key buffer is exactly
# the pattern a hand-rolled compare uses, and the default tolerates it.
valgrind --tool=memcheck \
	--error-exitcode=1 \
	--track-origins=yes \
	--partial-loads-ok=no \
	--num-callers=25 \
	--suppressions="$ROOT/tests/host/ct/host-aes.supp" \
	--log-file="$OUT/valgrind.log" \
	"$OUT/ct" >"$OUT/stdout.log" 2>&1
rc=$?

if [ "$rc" -ne 0 ]; then
	printf '  %s%s%s secret-dependent control flow or memory access\n\n' "$RED" "$CRS" "$RESET"
	# Only the memcheck findings, not the leak summary — a harness that exits without freeing is
	# not what this gate is about, and the leak block buries the finding.
	sed -n '/Conditional jump\|uninitialised value\|Use of uninitialised/,/^==.*$/p' \
		"$OUT/valgrind.log" | sed 's/^/      /' | head -60
	printf '\n      full log: build/ct/valgrind.log\n'
	printf '      A branch or table index that depends on key material leaks it through timing.\n'
	printf '      The fix is a branch-free form (see sp0_ct_diff in ccc_kdf.c for the pattern),\n'
	printf '      not a suppression: host-aes.supp covers the primitive only, on purpose.\n\n'
	exit 1
fi

printf '  %s%s%s no secret-dependent branch or index in the ladder\n\n' "$GRN" "$CHK" "$RESET"
exit 0
