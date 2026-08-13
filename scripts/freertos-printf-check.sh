#!/usr/bin/env bash
#
# Keep format specifiers newlib-nano cannot honour out of this image.
#
# This port links libc_nano.a, which the Arm GNU Toolchain builds without
# _WANT_IO_LONG_LONG and, under nano.specs, without the float formatter unless
# something pulls _printf_float in. A format this library does not implement is
# not a cosmetic problem:
#
#   "uptime %lld us, radio %s" with (long long) and two char * arguments
#
# _printf_i does not recognise ll, so it consumes four bytes where eight were
# passed. Every argument after it is then read one slot early, the %s takes the
# low word of the uptime as a char *, memchr dereferences it, and the precise
# bus fault lands in default_handler -- which spins. The RTC1 tick stops with
# it, so the board does not crash visibly. It prints a complete, healthy boot
# log and then goes silent, which is a much harder thing to find. It cost a
# debugging session on hardware, tracked from a frozen xTickCount back through
# a blocked work queue to CFSR = 0x8200 and BFAR holding a microsecond count.
#
# Only the code this port owns is checked. modules/ is shared with the Zephyr
# and ESP-IDF images, where these formats are correct and used deliberately, so
# hits there are reported as an advisory rather than a failure: they matter only
# if one of those files is ever linked into this image.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

OWNED=(
	"$ROOT/ports/freertos-nrf52833"
	"$ROOT/apps/dwm3001cdk-lock-freertos"
)
SHARED="$ROOT/modules"

# ll and L are the faults. The float conversions print nothing useful without
# _printf_float but do not corrupt the argument list, so they are named here
# too and fail for the same reason: silence in a log is a bug being hidden.
PATTERN='%[-+ #0-9.*]*(ll|L)[diouxX]|%[-+ #0-9.*]*[fFeEgGaA]'

fail=0

# Comment lines are dropped, because the files that explain this trap have to
# be able to name it. A comment cannot reach the formatter, so the only cost is
# that a specifier hidden inside a commented-out call goes unreported -- and
# commented-out code cannot fault either.
scan() { # <dir>
	grep -rInE "$PATTERN" --include='*.c' --include='*.h' "$1" 2>/dev/null |
		grep -vE '^[^:]*:[0-9]+:[[:space:]]*(\*|//|/\*)' || true
}

owned_hits=""
for dir in "${OWNED[@]}"; do
	[ -d "$dir" ] || continue
	hits="$(scan "$dir")"
	[ -n "$hits" ] && owned_hits="${owned_hits}${hits}"$'\n'
done

if [ -n "${owned_hits//[$'\n']/}" ]; then
	printf '  FAIL a format newlib-nano cannot honour reached this image:\n'
	printf '%s' "$owned_hits" | sed 's|^'"$ROOT"'/|    |'
	printf '    ll consumes the wrong argument width and bus-faults on the next %%s;\n'
	printf '    the float conversions print nothing without _printf_float.\n'
	printf '    Split the value into 32-bit parts at the call site.\n'
	fail=1
fi

if [ -d "$SHARED" ]; then
	shared_hits="$(scan "$SHARED" | wc -l | tr -d ' ')"
	if [ "$shared_hits" != "0" ]; then
		printf '  note: %s such formats in shared modules/, correct on Zephyr and\n' \
			"$shared_hits"
		printf '        ESP-IDF. They are a hazard only where linked into this image.\n'
	fi
fi

if [ "$fail" -eq 0 ]; then
	printf '  printf: no unsupported formats in port or app sources\n'
fi

exit "$fail"
