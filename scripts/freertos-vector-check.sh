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
ELF=${1:?usage: freertos-vector-check.sh <image.elf> [--without=<owner>]...}
shift
NM=${WOZ_ARM_TOOLCHAIN_DIR:+$WOZ_ARM_TOOLCHAIN_DIR/}arm-none-eabi-nm
YML=$(dirname "$0")/../ports/freertos-nrf52833/peripherals.yml

# A BUILD MAY LEAVE A SUBSYSTEM OUT; IT MAY NOT LEAVE THE CHECK OUT.
#
# peripherals.yml describes the full image. An image built without the
# provisioning console has no USBD vector and should not be asked for one --
# but "this vector is absent on purpose" has to be stated by the build that
# made it absent, by OWNER, not inferred here from the vector being missing.
# Inferring it is the fail-open shape this file exists to avoid: it would make
# a dropped handler and a deliberate exclusion indistinguishable.
EXCLUDED_OWNERS=""
for _arg in "$@"; do
    case "$_arg" in
        --without=*) EXCLUDED_OWNERS="$EXCLUDED_OWNERS ${_arg#--without=}" ;;
        *) echo "vector-check: unknown argument '$_arg'" >&2; exit 1 ;;
    esac
done

# READ from peripherals.yml, never restated here.
#
# This list used to be a hand-copied mirror, and it had already drifted: the yml
# gave USBD an owner and the copy did not name it at all -- neither required nor
# exempt, simply absent. Nothing was broken, because the nrfx driver supplies
# that vector. But had it ever stopped, the check would have reported "routed"
# while the console sat on the spin loop, which is the failure this file exists
# to catch. A mirror that has to be updated by hand is a ratchet that stops
# biting the moment someone forgets, and it reports success either way.
#
# Every entry under `interrupts:` must reach a real handler. `enabled: false`
# marks a priority RESERVATION with no routed vector -- SPIM3 is one, because
# the DW3110 bus is polled -- so those are excluded here rather than exempted
# by name. Peripherals owned in the `peripherals:` map but absent from
# `interrupts:` raise no interrupt in this image (TEMP is read synchronously,
# TIMER1 is driven inside the 802.15.4 service layer) and never reach this list.
[ -f "$YML" ] || { echo "vector-check: no peripherals.yml at $YML" >&2; exit 1; }

REQUIRED_PAIRS=$(awk '
    /^interrupts:/       { in_blk = 1; next }
    in_blk && /^[a-z]/   { in_blk = 0 }
    in_blk && /^[ \t]+[A-Z0-9_]+:/ {
        if ($0 ~ /enabled:[ \t]*false/) next
        name = $1; sub(/:$/, "", name)
        owner = "-"
        if (match($0, /owner:[ \t]*[A-Za-z0-9_]+/)) {
            owner = substr($0, RSTART, RLENGTH)
            sub(/owner:[ \t]*/, "", owner)
        }
        print name, owner
    }
' "$YML")

# Drop the excluded owners here, AFTER the completeness cross-check below has
# seen the file whole, and say which ones went. An exclusion nobody can see in
# the output is an exclusion that outlives the reason for it.
REQUIRED=""
excluded_seen=""
while read -r _name _owner; do
    [ -n "$_name" ] || continue
    _skip=0
    for _ex in $EXCLUDED_OWNERS; do
        if [ "$_owner" = "$_ex" ]; then
            _skip=1
            excluded_seen="$excluded_seen $_name"
        fi
    done
    [ "$_skip" -eq 1 ] || REQUIRED="$REQUIRED $_name"
done <<EOF
$REQUIRED_PAIRS
EOF

# An owner named on the command line that matches nothing in the yml is a
# selector that matches nothing, and those read as a clean scan. Refuse.
for _ex in $EXCLUDED_OWNERS; do
    if ! printf '%s\n' "$REQUIRED_PAIRS" | awk '{print $2}' | grep -qx "$_ex"; then
        printf 'vector-check: --without=%s names no owner in %s.\n' "$_ex" "$YML" >&2
        printf '  Nothing was excluded by it, so the flag is either stale or\n' >&2
        printf '  misspelled, and a build is being told a vector is absent on\n' >&2
        printf '  purpose when the file never gave that owner one.\n' >&2
        exit 1
    fi
done

# A NON-EMPTY PARSE IS NOT A COMPLETE ONE.
#
# The scanner above is block-scoped: it starts at `interrupts:` and stops at the
# next line beginning in column zero. Anything that ends the block early -- a
# lowercase key, a stray unindented line -- truncates the list silently. It came
# back non-empty, so a bare emptiness check passes it, and the summary then
# quotes a smaller number that still looks entirely reasonable. Measured: one
# unindented line above RNG drops this from 11 vectors to 6, and RNG, RTC1,
# RTC2, GPIOTE and USBD go unchecked while the line reads "6 routed".
#
# So count the entries a second way, with a selector that does not know where
# the block is. Every interrupt entry carries `priority:` and nothing else in
# this file does, so grep finds all of them wherever they sit. The two counts
# must agree, and the comparison includes the enabled:false reservations
# because the failure being caught is the SCANNER losing lines, not the yml
# choosing to exclude one.
parsed_all=$(awk '
    /^interrupts:/       { in_blk = 1; next }
    in_blk && /^[a-z]/   { in_blk = 0 }
    in_blk && /^[ \t]+[A-Z0-9_]+:/ { n++ }
    END { print n + 0 }
' "$YML")
declared=$(grep -cE '^[ \t]+[A-Z0-9_]+:.*priority:' "$YML" || true)

if [ "$parsed_all" -ne "$declared" ]; then
    printf 'vector-check: parsed %s interrupt entries, the file declares %s.\n' \
        "$parsed_all" "$declared" >&2
    printf '  The interrupts: block scanner stopped early -- almost always a line\n' >&2
    printf '  that starts in column zero inside the block. Refusing to check a\n' >&2
    printf '  subset and report it as the whole.\n' >&2
    exit 1
fi

[ -n "$REQUIRED" ] || { echo "vector-check: parsed no interrupts from $YML" >&2; exit 1; }

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
    printf '  Route each in board/startup_freertos.c, or -- if it needs no vector --\n' >&2
    printf '  mark it `enabled: false` in peripherals.yml with the reason.\n' >&2
    exit 1
fi
if [ -n "$excluded_seen" ]; then
    printf '  vectors: %s routed, all named by peripherals.yml (excluded:%s)\n' \
        "$(echo $REQUIRED | wc -w | tr -d ' ')" "$excluded_seen"
else
    printf '  vectors: %s routed, all named by peripherals.yml\n' \
        "$(echo $REQUIRED | wc -w | tr -d ' ')"
fi
