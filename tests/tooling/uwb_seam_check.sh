#!/usr/bin/env bash
#
# uwb_seam_check.sh — keep the CCC STS seam impossible to bypass.
#
# WHAT IS BEING PREVENTED. Four decadriver entry points carry engine behaviour
# that a caller must not skip (modules/woz_uwb/include/uwb_seam.h):
#
#   dwt_rxenable         arming RX must first program the CCC STS for the slot
#   dwt_configurestsiv   loading an STS-IV must substitute the CCC STS-V
#   dwt_setcallbacks     registering callbacks must insert the Pre-POLL shim,
#                        which is what warms the next block's STS at all
#   dwt_configure        a PHY (re)configuration is traced
#
# A call site that reaches past the seam is SILENT on the bench: the radio still
# arms, ranging still runs, the phone just never unlocks anything because the
# STS never matched. That is a bad afternoon to debug, and it is exactly the
# failure mode a link-time interposer used to make structurally impossible.
# This gate buys that guarantee back mechanically.
#
#   tests/tooling/uwb_seam_check.sh              # scan the tracked sources
#   tests/tooling/uwb_seam_check.sh --self-test  # prove the gate can actually fail
#   make check / make seam                       # runs it as the `uwb-seam` suite
#
# Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
#
# WHAT IS EXEMPT, and why each one is not a hole:
#
#   uwb_seam.h                 declares the helpers; the non-engine tier inlines
#                              straight to the driver, which IS the fallback
#   ccc_shim_rx.c              implements woz_uwb_arm_rx. Its own self-rearm
#   ccc_shim_wrap.c            implements woz_uwb_set_sts_iv          sites have
#   uwb_rxdiag.c               implements the other two               already
#   port/woz_seam_stubs.c      the ESP32 half of the same two         programmed
#                                                                     the STS
#   ccc_sts.c                  the register-level key/IV packer itself, with no
#                              production caller (host suites only)
#   modules/woz_dw3000/**      the vendor decadriver: it defines these
#   tests/**, docs/**            host doubles and prose
#
# Adding a file here is a decision to trust it forever. Prefer calling the seam.

set -euo pipefail

# Same shape as scripts/check-approtect.sh, the sibling gate this one mirrors.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	R=$'\033[31m' G=$'\033[32m' Z=$'\033[0m'
else
	R='' G='' Z=''
fi

cd "$(dirname "$0")/../.."

# One definition, used by the scan AND by the self-test. Written once on purpose:
# a self-test with its own copy of the pattern proves that the copy works.
SEAM_SYMBOLS='dwt_rxenable|dwt_configurestsiv|dwt_setcallbacks|dwt_configure'

# A call, not a mention: the symbol followed by an open paren. Declarations end
# in `;` and are matched too -- that is deliberate, a local `extern` of a seamed
# symbol is how a bypass gets written in the first place.
seam_call_re() { printf '(^|[^_[:alnum:]])(%s)[[:space:]]*\\(' "$SEAM_SYMBOLS"; }

# Prose naming a seam symbol is not a call. Drop lines that open with a comment
# marker, on `grep -n` output so the line numbers survive. A trailing comment on
# a line of code is left in: that line has real code on it either way.
COMMENT_LINE_RE='^[0-9]+:[[:space:]]*(\*|//|/\*)'

# The files allowed to call the raw entry points. See the header for why each.
EXEMPT_RE='^(modules/woz_uwb/include/uwb_seam\.h
|modules/woz_uwb/src/ccc/ccc_shim_rx\.c
|modules/woz_uwb/src/ccc/ccc_shim_wrap\.c
|modules/woz_uwb/src/ccc/ccc_sts\.c
|modules/woz_uwb/src/driver/uwb_rxdiag\.c
|ports/esp32/components/woz_uwb/port/woz_seam_stubs\.c
|modules/woz_dw3000/
|tests/)'
EXEMPT_RE=${EXEMPT_RE//$'\n'/}

# The tree the seam covers: our own engine + port sources. deps/ and tests/ are
# excluded above rather than here so the exemption list reads as one thing.
scan_paths() {
	git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' \
		'ports/*.c' 'ports/*.h' 'ports/*.cpp' 'apps/*.c' 'apps/*.h' 'apps/*.cpp' \
		'examples/*.c' 'examples/*.h' 'examples/*.cpp'
}

# ---- scan -------------------------------------------------------------------

# Scan source files for calls to CCC seam symbols that bypass uwb_seam.h and report findings with
# their line numbers. Return 0 if no violations are found, 1 otherwise.
scan() {
	local findings=0 f hits
	local re
	re=$(seam_call_re)

	while IFS= read -r f; do
		[[ $f =~ $EXEMPT_RE ]] && continue
		hits=$(grep -nE "$re" "$f" | grep -vE "$COMMENT_LINE_RE" || true)
		[ -n "$hits" ] || continue
		while IFS= read -r hit; do
			printf '%s  %s:%s%s\n' "$R" "$f" "$hit" "$Z"
			findings=$((findings + 1))
		done <<<"$hits"
	done < <(scan_paths)

	if [ "$findings" -gt 0 ]; then
		printf '%scheck-uwb-seam: %d call(s) reach past uwb_seam.h%s\n' \
			"$R" "$findings" "$Z" >&2
		printf '  Call woz_uwb_arm_rx / _set_sts_iv / _set_callbacks / _configure_phy\n' >&2
		printf '  instead, or add the file to EXEMPT_RE with a reason in the header.\n' >&2
		return 1
	fi
	printf '%s  ok   every caller reaches the radio through uwb_seam.h%s\n' "$G" "$Z"
	return 0
}

# The seam is only worth enforcing if the helpers exist to be called. A rename
# that left the callers behind would otherwise pass by finding nothing.
check_helpers() {
	local missing=0 h
	for h in woz_uwb_arm_rx woz_uwb_set_sts_iv woz_uwb_set_callbacks woz_uwb_configure_phy; do
		if ! grep -q "$h" modules/woz_uwb/include/uwb_seam.h 2>/dev/null; then
			printf '%s  uwb_seam.h does not declare %s%s\n' "$R" "$h" "$Z" >&2
			missing=$((missing + 1))
		fi
	done
	[ "$missing" -eq 0 ] || return 2
	printf '%s  ok   uwb_seam.h declares all four helpers%s\n' "$G" "$Z"
}

# ---- self-test --------------------------------------------------------------
#
# A gate whose fixture is wrong passes while checking nothing. Plant each shape
# the scan must catch and each shape it must ignore, and fail loudly on either.

self_test() {
	local re fails=0
	re=$(seam_call_re)

	local should_fire=(
		'	dwt_rxenable(DWT_START_RX_IMMEDIATE);'
		'	if (dwt_configure(&cfg) != DWT_SUCCESS) {'
		'	(void) dwt_setcallbacks (&cbs);'
		'	dwt_configurestsiv(&iv);'
		'int32_t dwt_rxenable(int32_t mode);'
	)
	local should_not[0]=
	should_not=(
		'	woz_uwb_arm_rx(DWT_START_RX_IMMEDIATE);'
		'	woz_uwb_configure_phy(&cfg);'
		' * dwt_rxenable refused the delayed RX (ARM FAIL not-late)'
		'#define RXDIAG_CFG_LOG 8'
		'	my_dwt_rxenable(mode);'
	)

	local line n=0
	for line in "${should_fire[@]}"; do
		if printf '%s\n' "$line" | grep -qE "$re"; then
			n=$((n + 1))
		else
			printf '%s  self-test FAILED: missed a bypass: %s%s\n' "$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: detector fires on all %d bypass shapes%s\n' \
		"$G" "$n" "$Z"

	local quiet=0
	for line in "${should_not[@]}"; do
		if printf '%s\n' "$line" | grep -qE "$re"; then
			printf '%s  self-test FAILED: fired on a legitimate line: %s%s\n' \
				"$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		else
			quiet=$((quiet + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: quiet on all %d legitimate shapes%s\n' \
		"$G" "$quiet" "$Z"

	# The comment filter runs on `grep -n` output, so it must drop a doc line and
	# keep a line of code that happens to carry a trailing comment.
	local drop=('15:	 * a later dwt_configure() re-runs it' '9://	dwt_rxenable(mode);')
	local keep='20:	woz_uwb_arm_rx(m); /* not dwt_rxenable(m) */'
	for line in "${drop[@]}"; do
		if ! printf '%s\n' "$line" | grep -qE "$COMMENT_LINE_RE"; then
			printf '%s  self-test FAILED: comment filter kept: %s%s\n' "$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	if printf '%s\n' "$keep" | grep -qE "$COMMENT_LINE_RE"; then
		printf '%s  self-test FAILED: comment filter dropped a line of code%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	[ "$fails" -ne 0 ] || printf '%s  self-test: comment filter drops prose, keeps code%s\n' \
		"$G" "$Z"

	# The exemption list is only safe if it is exact. A prefix that swallowed a
	# whole directory would silence the gate without anyone noticing.
	local f
	for f in modules/woz_uwb/src/driver/uwb_min.c modules/woz_uwb/src/driver/uwb_isr.c; do
		if [[ $f =~ $EXEMPT_RE ]]; then
			printf '%s  self-test FAILED: %s is exempt, but it is a caller%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: the exemption list still covers only the seam%s\n' \
		"$G" "$Z"

	if [ "$fails" -ne 0 ]; then
		printf '%scheck-uwb-seam: the gate itself is broken%s\n' "$R" "$Z" >&2
		return 2
	fi
	return 0
}

case "${1-}" in
--self-test)
	self_test
	;;
"")
	check_helpers || exit $?
	scan
	;;
*)
	printf 'usage: %s [--self-test]\n' "$0" >&2
	exit 2
	;;
esac
