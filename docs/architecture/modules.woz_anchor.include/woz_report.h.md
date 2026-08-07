<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_report.h`

@file woz_report.h — one range report, as a line of ASCII.
Stage C2 of internal/two-anchor-plan.md. The satellite already has a console
the door node does not, so the cheapest possible two-anchor data stream is
one line per accepted round, read by a host script. No transport firmware, no
mesh, no sockets: a cable and a grep.
The same struct is what a Stage C3 datagram would carry, so the fields are
chosen for the binary format the plan specifies rather than for the text one.
That is deliberate -- when the transport changes, only the codec changes.
## The line
ARP1 <anchor> <seq> <us_hi> <us_lo> <d_mm> <q> <trust> <flags> <drop> <acc> *<crc>
Space-separated decimals, one trailing CRC-16/CCITT-FALSE in four uppercase
hex digits, over every byte before the '*'. Terminated by '\n'.
WHY A CHECKSUM ON A CABLE. A UART line that gets corrupted usually fails to
parse and is discarded, which is harmless. The case worth defending against
is the one that does not: a single flipped digit turns 1004 mm into 1904 mm
and stays perfectly well-formed. This feeds a security decision, so a
plausible wrong number is worse than an obvious broken one.
WHY THE UPTIME IS TWO FIELDS. It is a 64-bit microsecond count, and printing
one on an embedded target means depending on the C library having 64-bit
integer conversion compiled in -- which is a Kconfig away from not being
true, silently, at the point where the line becomes garbage. Splitting it
into two 32-bit halves removes the dependency. For the same reason this file
does its own decimal conversion and does not call snprintf at all.
WHY THE ROUND SEQUENCE IS THE TIMEBASE. Both anchors take part in the same
numbered DS-TWR round, so a shared exact index already exists and no clock
needs synchronising. The uptime rides along only so drift can be measured,
and a later step can decide whether it matters.

**used by** [`modules/woz_anchor/src/woz_report.c`](../modules.woz_anchor.src/woz_report.c.md)

## API

### `struct woz_range_report`
`modules/woz_anchor/include/woz_report.h:57`

One accepted DS-TWR round, as the satellite saw it.
