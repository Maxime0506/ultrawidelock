#!/bin/sh
# Every vector peripherals.yml gives an owner must reach that owner.
#
# The vector table is DATA. A weak alias that no owner ever overrode links
# cleanly, passes every host test, and resolves to default_handler -- whose body
# is an infinite loop. The first interrupt from that peripheral then parks the
# core with the interrupt still latched, and the board simply stops.
#
# That is not hypothetical: RNG and RTC2 both shipped in exactly that state and
# were found only by running the image on a board, when the SoftDevice
# Controller asked for entropy. This check is what makes the next one a build
# failure instead of a debugging session.
set -eu
ELF=${1:?usage: freertos-vector-check.sh <image.elf>}
NM=${ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR:+$ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR/}arm-none-eabi-nm

# Mirrors the `peripherals:` map in ports/freertos-nrf52833/peripherals.yml.
REQUIRED="POWER_CLOCK RADIO TIMER0 RTC0 SWI5_EGU5 RTC1 RTC2 SWI0_EGU0 RNG GPIOTE"

# Owned in peripherals.yml but deliberately without a vector, each for a reason:
#   TEMP   MPSL reads the sensor synchronously; it raises no interrupt here.
#   TIMER1 the MPSL-arbitrated 802.15.4 service layer drives it internally and
#          exports no handler to route -- verified absent from the image.
#   SPIM3  the DW3110 bus is polled; peripherals.yml marks it enabled: false.
EXEMPT="TEMP TIMER1 SPIM3"

DH=$("$NM" "$ELF" | awk '$3=="default_handler"{print $1}')
[ -n "$DH" ] || { echo "vector-check: no default_handler in $ELF" >&2; exit 1; }

rc=0
for v in $REQUIRED; do
    sym="${v}_IRQHandler"
    addr=$("$NM" "$ELF" | awk -v s="$sym" '$3==s{print $1}')
    if [ -z "$addr" ]; then
        printf '  MISSING  %-28s no such symbol in the image\n' "$sym" >&2
        rc=1
    elif [ "$addr" = "$DH" ]; then
        printf '  UNOWNED  %-28s resolves to default_handler (spin loop)\n' "$sym" >&2
        rc=1
    fi
done

if [ "$rc" -ne 0 ]; then
    printf '\n  peripherals.yml gives these vectors an owner and the image does not.\n' >&2
    printf '  Route each in board/startup_freertos.c, or move it to EXEMPT here\n' >&2
    printf '  with the reason it needs no handler.\n' >&2
    exit 1
fi
printf '  vectors: %s routed, %s exempt by name\n' "$(echo $REQUIRED | wc -w | tr -d ' ')" "$(echo $EXEMPT | wc -w | tr -d ' ')"
