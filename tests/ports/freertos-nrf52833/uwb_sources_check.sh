#!/usr/bin/env bash
#
# Hold the port's UWB source manifest to what it claims.
#
# ports/freertos-nrf52833/uwb/sources.mk is a declaration with no compiler
# behind it yet: the target build graph consumes it, and until that graph links
# the UWB engine, a path that does not exist or a configuration that selects the
# wrong backend is invisible. So it is checked here rather than discovered at
# the first link.
#
# Three things are checked, and each is a way this file goes wrong quietly:
#
#   every path resolves     a manifest entry that names a moved file compiles
#                           fine on the ports that do not read it
#   the PSA backend         this port has a PSA provider and the ESP-IDF one
#                           does not, so copying that port's selection would
#                           link a second path to the same primitive
#   the Final snapshot      not a diagnostic on this part: without it the
#                           ranges are kilometres wide, on a single core where
#                           BLE shares the CPU with the ranging callbacks
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MK="${WOZ_UWB_SOURCES_MK:-$ROOT/ports/freertos-nrf52833/uwb/sources.mk}"

checks=0
failures=0

check() { # <label> <status>
	checks=$((checks + 1))
	if [ "$2" -eq 0 ]; then
		printf '  ok   %s\n' "$1"
	else
		failures=$((failures + 1))
		printf '  FAIL %s\n' "$1"
	fi
}

# Ask make itself, rather than re-parsing the fragment: a check that
# reimplements the expansion is checking its own copy of it.
ask() { # <variable> -> its expansion, one entry per line
	printf 'REPO_ROOT := %s\ninclude %s\n.PHONY: show\nshow:\n\t@printf "%%s\\n" $(%s)\n' \
		"$ROOT" "$MK" "$1" | make --no-print-directory -f - show
}

# ---- self-test --------------------------------------------------------------
# The checks below pass against a manifest that is right, and they would also
# pass against a manifest they had quietly stopped reading. So each one is shown
# a manifest carrying the defect it exists to catch, and has to fail.
tmp=
self_test() {
	local mutants=0 survived=0 i=0

	# Not local: the EXIT trap runs after this function's scope is gone.
	tmp=$(mktemp -d)
	trap 'rm -rf "$tmp"' EXIT

	mutate() { # <description> <sed expression>
		local desc=$1 expr=$2 mk="$tmp/mutant_$i.mk"

		i=$((i + 1))
		mutants=$((mutants + 1))
		sed -e "$expr" "$MK" >"$mk"
		if cmp -s "$mk" "$MK"; then
			printf '  FAIL mutation changed nothing: %s\n' "$desc"
			survived=$((survived + 1))
			return
		fi
		if WOZ_UWB_SOURCES_MK="$mk" "$0" >"$tmp/out" 2>&1; then
			printf '  FAIL survives the check: %s\n' "$desc"
			survived=$((survived + 1))
		else
			printf '  ok   caught: %s\n' "$desc"
		fi
	}

	mutate "a role manifest that no longer exists" \
		's|responder_engine.list|responder_engine_gone.list|'
	mutate "a platform backend named by a path that moved" \
		's|uwb/dw3000_spi_freertos.c|uwb/dw3000_spi.c|'
	mutate "the hardware backend dropped from the set" \
		'/dw3000_hw_freertos.c/d'
	mutate "an include directory that does not exist" \
		's|modules/woz_uwb/src/facade|modules/woz_uwb/src/facade_gone|'
	mutate "the mbedTLS crypto backend copied over from the ESP-IDF port" \
		's|crypto_psa.list|crypto_mbedtls.list|'
	mutate "the mbedTLS define left selected beside the PSA one" \
		's|CONFIG_WOZ_CRYPTO_PSA=1|CONFIG_WOZ_CRYPTO_PSA=1 CONFIG_WOZ_CRYPTO_MBEDTLS=1|'
	mutate "the Final-capture snapshot dropped as if it were a diagnostic" \
		'/CONFIG_WOZ_UWB_FINAL_SNAPSHOT=1/d'
	mutate "the engine built as something other than a responder" \
		's|CONFIG_WOZ_UWB_RESPONDER=1|CONFIG_WOZ_UWB_INITIATOR=1|'
	mutate "the DW3720 device layer selected instead of the DW3000" \
		's|CONFIG_DW3000_CHIP_DW3000=1|CONFIG_DW3000_CHIP_DW3720=1|'
	mutate "the Zephyr backend dragged in beside this port's own" \
		's|ports/freertos-nrf52833/uwb/dw3000_hw_freertos.c|ports/zephyr/dw3000/dw3000_hw.c|'

	printf 'uwb-sources-self-test: %s (%d mutations)\n' \
		"$([ "$survived" -eq 0 ] && echo PASS || echo FAIL)" "$mutants"
	[ "$survived" -eq 0 ]
}

if [ "${1:-}" = "--self-test" ]; then
	self_test
	exit
fi

srcs=$(ask WOZ_UWB_SRCS)
incs=$(ask WOZ_UWB_INCLUDES)
defs=$(ask WOZ_UWB_DEFINES)
lists=$(ask WOZ_UWB_ROLE_LISTS)

[ -n "$srcs" ]
check "the source set is not empty" $?

missing=0
while IFS= read -r path; do
	[ -n "$path" ] || continue
	if [ ! -f "$path" ]; then
		printf '       missing source: %s\n' "$path"
		missing=1
	fi
done <<EOF
$srcs
EOF
check "every source the manifest names exists" "$missing"

# A manifest that has been renamed or deleted expands to nothing rather than to
# an error, so the role's sources vanish and the check above still passes: it
# only ever sees the paths that survived. The manifests themselves are the thing
# to assert on.
missing=0
while IFS= read -r path; do
	[ -n "$path" ] || continue
	if [ ! -f "$path" ]; then
		printf '       missing role manifest: %s\n' "$path"
		missing=1
	fi
done <<EOF
$lists
EOF
check "every role manifest the port reads exists" "$missing"

missing=0
while IFS= read -r path; do
	[ -n "$path" ] || continue
	if [ ! -d "$path" ]; then
		printf '       missing include directory: %s\n' "$path"
		missing=1
	fi
done <<EOF
$incs
EOF
check "every include directory the manifest names exists" "$missing"

# The two platform backends this port owns, and nothing standing in for them.
case "$srcs" in
*ports/freertos-nrf52833/uwb/dw3000_spi_freertos.c*) r=0 ;;
*) r=1 ;;
esac
check "the port's own SPI backend is in the source set" "$r"

case "$srcs" in
*ports/freertos-nrf52833/uwb/dw3000_hw_freertos.c*) r=0 ;;
*) r=1 ;;
esac
check "the port's own hardware backend is in the source set" "$r"

# And not the Zephyr or ESP-IDF ones, which implement the same symbols.
case "$srcs" in
*ports/zephyr/*|*ports/esp32/*) r=1 ;;
*) r=0 ;;
esac
check "no other port's backend is dragged in beside them" "$r"

case "$defs" in
*CONFIG_WOZ_CRYPTO_PSA=1*) r=0 ;;
*) r=1 ;;
esac
check "the AES-ECB seam takes the PSA provider this port has" "$r"

case "$defs" in
*CONFIG_WOZ_CRYPTO_MBEDTLS*) r=1 ;;
*) r=0 ;;
esac
check "the mbedTLS variant is not selected beside it" "$r"

case "$srcs" in
*modules/woz_uwb/src/ccc/ccc_crypto_psa.c*) r=0 ;;
*) r=1 ;;
esac
check "the PSA crypto source is the one in the set" "$r"

case "$defs" in
*CONFIG_WOZ_UWB_FINAL_SNAPSHOT=1*) r=0 ;;
*) r=1 ;;
esac
check "the Final-capture snapshot is on, as this single-core part requires" "$r"

case "$defs" in
*CONFIG_WOZ_UWB_RESPONDER=1*) r=0 ;;
*) r=1 ;;
esac
check "the engine is built as a responder" "$r"

case "$defs" in
*CONFIG_DW3000_CHIP_DW3000=1*) r=0 ;;
*) r=1 ;;
esac
check "the DW3000 device layer is selected, not the DW3720 twin" "$r"

# The snapshot is compiled in, so the code it guards has to be reachable.
if grep -q 'CONFIG_WOZ_UWB_FINAL_SNAPSHOT' "$ROOT/modules/woz_uwb/src/ccc/ccc_shim_rx.c"; then
	r=0
else
	r=1
fi
check "the shim still gates its Final-capture path on that symbol" "$r"

printf 'uwb-sources: %s (%d checks)\n' \
	"$([ "$failures" -eq 0 ] && echo PASS || echo FAIL)" "$checks"
[ "$failures" -eq 0 ]
