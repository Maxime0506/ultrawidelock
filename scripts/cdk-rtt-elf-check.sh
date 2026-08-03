#!/usr/bin/env bash
# Refuse to attach RTT with an ELF the board is not running.
#
# probe-rs reads the _SEGGER_RTT control-block address out of the ELF you hand
# it. Hand it one you built but did not flash and it reads an address the board
# never populated, then prints nothing -- which looks exactly like a dead board.
# That failure has cost real bench time, so `make monitor` checks first.
#
# The predicate is the _SEGGER_RTT address, not the file bytes. Two ELFs that
# place the control block identically stream fine no matter how else they
# differ, and a byte compare would refuse those too -- false refusals are how a
# guard gets routed around.
#
# Exit 1 ONLY on a positive mismatch: two addresses that were both read and
# disagree. Anything that leaves the question open (no record of a flash, no
# toolchain nm, no symbol) warns and exits 0, because blocking a console on an
# indeterminate check is worse than the bug.
#
# Usage: cdk-rtt-elf-check.sh <candidate-elf> <deployed-elf>
set -euo pipefail

CAND="${1:?usage: cdk-rtt-elf-check.sh <candidate-elf> <deployed-elf>}"
DEP="${2:?usage: cdk-rtt-elf-check.sh <candidate-elf> <deployed-elf>}"
SYM="${CDK_RTT_SYMBOL:-_SEGGER_RTT}"

warn() { printf '  %s\n' "$*" >&2; }

if [ ! -f "$DEP" ]; then
	warn "no record of what was flashed ($DEP)"
	warn "cannot confirm this ELF matches the board -- \`make flash\` records it"
	exit 0
fi

# Same resolution `make ota-window` uses: the Zephyr SDK's nm, whatever
# toolchain version is installed. NM overrides it for tests.
nm="${NM:-$(ls /opt/nordic/ncs/toolchains/*/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm 2>/dev/null | head -1 || true)}"
if [ -z "$nm" ] || [ ! -x "$nm" ]; then
	warn "no arm-zephyr-eabi-nm found -- skipping the stale-ELF check"
	exit 0
fi

# nm prints "ADDRESS TYPE NAME"; undefined symbols have no address and so never
# reach field 3, which is why an exact $3 match is enough.
addr_of() { "$nm" "$1" 2>/dev/null | awk -v s="$SYM" '$3 == s { print $1; exit }'; }

cand_addr="$(addr_of "$CAND" || true)"
dep_addr="$(addr_of "$DEP" || true)"

if [ -z "$cand_addr" ] || [ -z "$dep_addr" ]; then
	warn "$SYM not found in both ELFs -- skipping the stale-ELF check"
	exit 0
fi

if [ "$cand_addr" != "$dep_addr" ]; then
	warn "STALE ELF -- this is not what the board is running, RTT would print nothing"
	warn "  attaching with : $CAND  ($SYM at 0x$cand_addr)"
	warn "  board is running: $DEP  ($SYM at 0x$dep_addr)"
	warn ""
	warn "Either flash this image (\`make flash\` or \`make dfu\`), or attach with the"
	warn "one on the board: make monitor CDK_RTT_BUILD=<the build dir you flashed>"
	exit 1
fi

exit 0
