#!/usr/bin/env bash
#
# check-approtect.sh — refuse to ship an image that locks APPROTECT.
#
# WHAT IS BEING PREVENTED. On the nRF52833 (and the nRF5340), selecting
# CONFIG_NRF_APPROTECT_LOCK makes SystemInit() lock the firmware branch of the
# APPROTECT mechanism on EVERY boot, before any of our code runs. The only way
# back is `nrfjprog --recover`, which mass-erases flash AND UICR. On this
# project that is not "lose the firmware" -- it is:
#
#   * settings_storage (0x7e000) gone, so the Matter fabrics and trust anchors go
#   * the reader private key gone (apps/dwm3001cdk-lock/src/prov_shell.c), and
#     EVERY iPhone key already provisioned against this board dies with it
#
# A board that has done this is not bricked, but every future debug session
# costs a full wipe and a re-provision, and the credentials cannot be recreated.
# NCS defaults to open (NRF_APPROTECT_USE_UICR); the requirement is only that
# nobody turns it on. This gate is what makes "nobody" true.
#
#   scripts/check-approtect.sh              # the two config layers
#   scripts/check-approtect.sh --device SNR # what the attached board is ACTUALLY in
#   scripts/check-approtect.sh --self-test  # prove the gate can actually fail
#
# Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
#
# THREE LAYERS, because any one alone is a gate that passes while checking
# nothing:
#
#   sources    Every tracked config file. This is the layer that works in CI,
#              which never builds firmware (firmware-builds.yml is
#              workflow_dispatch only), so a .config scan there would find zero
#              files and report success against nothing.
#
#   generated  Every */zephyr/.config that exists locally. This is the layer
#              that catches what the source scan CANNOT: the setting arriving
#              from a board defconfig, an SoC Kconfig default, or a sysbuild
#              set_config_bool -- none of which appear anywhere in this tree.
#              Checking the generated config is the only way to know what was
#              actually compiled, which is why the source layer never stands in
#              for it.
#
#   device     What the SILICON is in right now, read back over the probe.
#              Opt-in (--device SNR) because it needs a board attached.
#
# The generated layer reporting "0 builds examined" is NOT a pass and is not
# silent: it says so, and it is the reason the source layer is not optional.
#
# WHY THE DEVICE LAYER EXISTS, which is the expensive lesson. Both config layers
# answer "did our firmware ask for the lock". Neither answers "is this board
# locked", and those come apart: a mass erase leaves UICR blank, and on the
# nRF5340 a blank UICR reads as APPROTECT ENGAGED until firmware writes it open
# again. On 2026-08-03 an nRF5340 DK sat in exactly that state while this gate
# reported "3 generated image config(s) examined, all open", which was true and
# useless. The probe then served partial reads: RAM below ~0x20057000 read back
# fine and everything above it returned "memory protection issue", which reads
# exactly like a board with 100 KB of RAM missing. Hours went into a hardware
# theory for what was a protection state, and `nrfutil device recover` cleared it
# in one command. Ask the silicon.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	R=$'\033[31m' G=$'\033[32m' Y=$'\033[33m' D=$'\033[2m' Z=$'\033[0m'
else
	R='' G='' Y='' D='' Z=''
fi

# The four symbols named in the standing rule. The SECURE_ pair is unreachable
# on the nRF52833 -- zephyr/soc/nordic/Kconfig gates that whole choice on
# SOC_NRF5340_CPUAPP || SOC_NRF54L_CPUAPP_COMMON || SOC_SERIES_NRF91 -- but this
# repo also builds the nRF5340, where it is reachable, so all four are checked
# everywhere rather than per-target.
IMAGE_SYMS='CONFIG_NRF_APPROTECT_LOCK|CONFIG_NRF_SECURE_APPROTECT_LOCK'
SB_SYMS='SB_CONFIG_APPROTECT_LOCK|SB_CONFIG_SECURE_APPROTECT_LOCK'

# One definition, used by the scan AND by the self-test. Written once on purpose:
# a self-test with its own copy of the pattern proves that the copy works, which
# is the failure mode where a gate stays green while checking nothing.
SET_RE="^[[:space:]]*(${IMAGE_SYMS}|${SB_SYMS})=y|set[[:space:]]*\([[:space:]]*(${IMAGE_SYMS}|${SB_SYMS})[[:space:]]+(y|ON|TRUE)"

findings=0

# ---- layer 1: tracked sources ---------------------------------------------
# Matches an ASSIGNMENT, not a mention. The distinction is the whole difficulty
# of this layer: the two files that enforce this rule -- CMakeLists.txt and
# sysbuild.cmake -- both name all four symbols in an `if()` and interpolate them
# into a FATAL_ERROR string, and a scan that merely greps for the symbol reports
# the guard as the violation. That is not a hypothetical; it is what the first
# version of this function did.
#
# So exactly two shapes count, and nothing else does:
#   Kconfig fragment   SYMBOL=y            (optionally indented)
#   CMake              set(SYMBOL y|ON|TRUE ...)
# `if(SYMBOL)`, "${SYMBOL}" and any comment are reads, not writes, and are
# ignored. Anything that sets one of these for real takes one of the two forms.
scan_sources() {
	local files hits
	files="$(git ls-files -- '*.conf' '*.overlay' '*.yml' '*.yaml' '*.cmake' \
		'CMakeLists.txt' '*/CMakeLists.txt' 'Kconfig*' '*/Kconfig*' 2>/dev/null)"
	[ -n "$files" ] || return 0

	hits="$(printf '%s\n' "$files" \
		| xargs grep -nE "$SET_RE" 2>/dev/null \
		| grep -vE ':[0-9]+:[[:space:]]*#' || true)"

	if [ -n "$hits" ]; then
		printf '%s  APPROTECT set in tracked sources:%s\n' "$R" "$Z"
		printf '%s\n' "$hits" | sed 's/^/      /'
		return 1
	fi
	return 0
}

# ---- layer 2: generated .config -------------------------------------------
# Every image of every build tree, including MCUboot's own -- which is the whole
# reason this layer exists now. MCUboot is a config file written from scratch,
# so it is the most likely place for the setting to appear, and it is not
# covered by any grep of the application's sources.
scan_generated() {
	local configs n hits
	configs="$(find . -path ./workspace -prune -o -name '.config' -path '*/zephyr/*' -print 2>/dev/null)"
	n="$(printf '%s' "$configs" | grep -c . || true)"

	if [ "$n" -eq 0 ]; then
		printf '%s  no generated .config found — 0 images examined%s\n' "$Y" "$Z"
		printf '%s      not a pass: this layer is the only one that sees a board defconfig\n' "$D"
		printf '      or an SoC default. Build first to exercise it.%s\n' "$Z"
		return 0
	fi

	hits="$(printf '%s\n' "$configs" | xargs grep -lE "^(${IMAGE_SYMS})=y" 2>/dev/null || true)"
	if [ -n "$hits" ]; then
		printf '%s  APPROTECT LOCKED in a generated config:%s\n' "$R" "$Z"
		printf '%s\n' "$hits" | while read -r f; do
			printf '      %s\n' "$f"
			grep -nE "^(${IMAGE_SYMS})=y" "$f" | sed 's/^/          /'
		done
		return 1
	fi

	printf '%s  %d generated image config(s) examined, all open%s\n' "$D" "$n" "$Z"
	return 0
}

# ---- layer 3: the attached device ------------------------------------------
# Reads the readback-protection state out of the silicon rather than out of any
# file we wrote. Exit 2 (could not check) is deliberately distinct from exit 1
# (found a lock): no probe attached is not evidence of anything, and must never
# be reported as a pass.
#
# The parse looks for the ENABLED phrasing rather than the disabled one, so an
# unrecognised nrfutil output reads as "cannot tell" instead of "fine". A gate
# that treats unfamiliar output as success is the failure this file exists about.
DEVICE_OK_RE='[Dd]ebug access is enabled'
DEVICE_BAD_RE='[Dd]ebug access is (currently )?disabled'

scan_device() {
	local snr="$1" out
	command -v nrfutil >/dev/null 2>&1 || {
		printf '%s  nrfutil not on PATH, device state NOT checked%s\n' "$Y" "$Z"
		return 2
	}
	out="$(nrfutil device protection-get --serial-number "$snr" 2>&1)" || {
		printf '%s  could not read protection state from %s%s\n' "$Y" "$snr" "$Z"
		printf '%s%s%s\n' "$D" "$(printf '%s' "$out" | sed 's/^/      /')" "$Z"
		return 2
	}

	if printf '%s' "$out" | grep -qE "$DEVICE_BAD_RE"; then
		printf '%s  APPROTECT ENGAGED on device %s:%s\n' "$R" "$snr" "$Z"
		printf '%s\n' "$out" | sed 's/^/      /'
		printf '%s  The board is locked NOW. Debug reads will be partial rather than\n' "$R"
		printf '  refused outright, so this masquerades as broken hardware.\n'
		printf '  Clear it with:  nrfutil device recover --serial-number %s\n' "$snr"
		printf '  That mass-erases flash AND UICR, taking settings_storage with it,\n'
		printf '  so export the identity first if the board is provisioned.%s\n' "$Z"
		return 1
	fi

	if printf '%s' "$out" | grep -qE "$DEVICE_OK_RE"; then
		printf '%s  device %s: debug access enabled%s\n' "$D" "$snr" "$Z"
		return 0
	fi

	printf '%s  unrecognised protection output for %s, NOT treated as a pass%s\n' \
		"$Y" "$snr" "$Z"
	printf '%s\n' "$out" | sed 's/^/      /'
	return 2
}

# ---- self-test -------------------------------------------------------------
# The lesson this exists for: a gate whose test fixture is wrong passes while
# checking nothing, and looks identical to a gate that works. So prove the
# detector fires on a planted positive before trusting a clean run.
self_test() {
	local tmp rc=0
	tmp="$(mktemp -d)" || return 2
	# shellcheck disable=SC2064
	trap "rm -rf '$tmp'" RETURN

	mkdir -p "$tmp/build-fake/zephyr"
	# The planted symbol is ASSEMBLED, never written literally. Two reasons, and
	# the second is the one that bit: a literal here would force this file to
	# exempt itself from its own source scan, and a gate with an exemption has a
	# hole exactly the shape of the thing it is guarding. (The first reason is
	# that a bare grep for the enabled form, over the whole tree, should come
	# back empty -- so "is it set anywhere?" is answerable without reading any
	# surrounding context to decide which hits were only fixtures.)
	local planted="CONFIG_NRF_APPROTECT""_LOCK=y"
	printf 'CONFIG_FOO=y\n%s\n' "$planted" > "$tmp/build-fake/zephyr/.config"
	if (cd "$tmp" && find . -name '.config' -path '*/zephyr/*' \
		-exec grep -lE "^(${IMAGE_SYMS})=y" {} + >/dev/null 2>&1); then
		printf '%s  self-test: detector fires on a planted lock%s\n' "$G" "$Z"
	else
		printf '%s  self-test FAILED: planted lock not detected%s\n' "$R" "$Z"
		rc=1
	fi

	printf 'CONFIG_FOO=y\n# %s is not set\n' "${planted%=y}" \
		> "$tmp/build-fake/zephyr/.config"
	if (cd "$tmp" && find . -name '.config' -path '*/zephyr/*' \
		-exec grep -lE "^(${IMAGE_SYMS})=y" {} + >/dev/null 2>&1); then
		printf '%s  self-test FAILED: fired on an unset symbol%s\n' "$R" "$Z"
		rc=1
	else
		printf '%s  self-test: quiet on `is not set`%s\n' "$G" "$Z"
	fi

	# The source layer, against the SAME $SET_RE the real scan uses. Every line
	# below is assembled rather than written out, for the reason given above.
	local sym="CONFIG_NRF_APPROTECT""_LOCK" sb="SB_CONFIG_APPROTECT""_LOCK"
	local must_fire must_not
	must_fire="$tmp/fire.txt"
	must_not="$tmp/quiet.txt"
	{
		printf '%s=y\n' "$sym"
		printf '\t%s=y\n' "$sb"
		printf 'set(%s y)\n' "$sym"
		printf 'set( %s ON )\n' "$sb"
	} > "$must_fire"
	{
		# The two guard shapes, which must NEVER be read as a violation, plus
		# the forms that mean it is off.
		printf 'if(%s OR %s)\n' "$sym" "$sb"
		printf '\t"  %s = ${%s}\\n"\n' "$sym" "$sym"
		printf '%s=n\n' "$sym"
		printf '# %s is not set\n' "$sym"
		printf '# %s=y in a comment\n' "$sym"
	} > "$must_not"

	local n_fire n_quiet
	n_fire="$(grep -cE "$SET_RE" "$must_fire" || true)"
	n_quiet="$(grep -E "$SET_RE" "$must_not" | grep -vE '^[[:space:]]*#' | grep -c . || true)"

	if [ "$n_fire" -eq 4 ]; then
		printf '%s  self-test: source scan fires on all 4 assignment shapes%s\n' "$G" "$Z"
	else
		printf '%s  self-test FAILED: source scan matched %s/4 assignments%s\n' \
			"$R" "$n_fire" "$Z"
		rc=1
	fi
	if [ "$n_quiet" -eq 0 ]; then
		printf '%s  self-test: source scan ignores the FATAL_ERROR guard itself%s\n' "$G" "$Z"
	else
		printf '%s  self-test FAILED: source scan flagged %s guard/off line(s)%s\n' \
			"$R" "$n_quiet" "$Z"
		rc=1
	fi

	# The device layer, against the real nrfutil phrasings. Checked against the
	# SAME two patterns scan_device uses, for the reason given at the top of this
	# function. The third case is the one that matters most: output the parser
	# does not recognise must NOT read as a pass, because that is how a board that
	# is quietly locked gets waved through.
	local locked open unknown
	locked='	access status: Debug access is currently disabled (status value: All)'
	open='	access status: Debug access is enabled (status value: None)'
	unknown='	access status: something nrfutil has not said before'

	if printf '%s' "$locked" | grep -qE "$DEVICE_BAD_RE"; then
		printf '%s  self-test: device layer fires on a locked board%s\n' "$G" "$Z"
	else
		printf '%s  self-test FAILED: device layer missed a locked board%s\n' "$R" "$Z"
		rc=1
	fi
	if printf '%s' "$open" | grep -qE "$DEVICE_OK_RE" \
		&& ! printf '%s' "$open" | grep -qE "$DEVICE_BAD_RE"; then
		printf '%s  self-test: device layer quiet on an open board%s\n' "$G" "$Z"
	else
		printf '%s  self-test FAILED: device layer misread an open board%s\n' "$R" "$Z"
		rc=1
	fi
	if printf '%s' "$unknown" | grep -qE "$DEVICE_OK_RE"; then
		printf '%s  self-test FAILED: unknown output read as a pass%s\n' "$R" "$Z"
		rc=1
	else
		printf '%s  self-test: device layer refuses to pass unknown output%s\n' "$G" "$Z"
	fi
	return $rc
}

case "${1:-}" in
--self-test)
	self_test
	exit $?
	;;
--device)
	[ -n "${2:-}" ] || { printf 'usage: %s --device <serial-number>\n' "$0" >&2; exit 2; }
	printf '%sapprotect%s  what device %s is actually in\n' "$D" "$Z" "$2"
	scan_device "$2"
	exit $?
	;;
'') ;;
*)
	printf 'usage: %s [--self-test | --device <serial-number>]\n' "$0" >&2
	exit 2
	;;
esac

printf '%sapprotect%s  the lock that costs a wipe and every provisioned iPhone key\n' \
	"$D" "$Z"
scan_sources || findings=1
scan_generated || findings=1

if [ "$findings" -ne 0 ]; then
	printf '%s  FAIL — remove the setting. Never `nrfjprog --recover` to undo it\n' "$R"
	printf '         on a provisioned board without exporting the identity first\n'
	printf '         (`aliro export yes`).%s\n' "$Z"
	exit 1
fi
printf '%s  ok%s\n' "$G" "$Z"
exit 0
