<!-- generated documentation — edit the source, not this file -->
# `tools/anchor_c2.py`

Anchor C2: read the satellite's ARP1 range-report lines and turn them into CSV.

Usage: python3 tools/anchor_c2.py <serial-port> [--baud N] [--csv out.csv] [--quiet]
       python3 tools/anchor_c2.py -            # read lines from stdin
       python3 tools/anchor_c2.py <log-file>   # replay a captured console log

Stage C2 of internal/two-anchor-plan.md. The satellite has a UART console the
door node does not, so the cheapest possible two-anchor data stream is one line
per accepted round, read here. No transport firmware, no mesh, no sockets.

The line format is defined by modules/woz_anchor/include/woz_report.h and the
CRC-16/CCITT-FALSE below is the same polynomial woz_report_crc16() implements.
Lines that fail the CRC are counted and dropped, never repaired: a flipped digit
that leaves a well-formed line is the exact case the checksum exists for.

Everything that is not an ARP1 line is ignored, so pointing this at a live
console full of Zephyr log output is the intended way to use it.

Prints a summary on exit (Ctrl-C is a normal way to stop):

  lines 4102  accepted 4038  crc-fail 2  malformed 0  gaps 12 (0.3%)

`gaps` counts missing round sequence numbers, which is the loss measurement the
plan's pass criterion is stated in.

**depends on** [`tools/tui/src/serial.ts`](../tools.tui.src/serial.ts.md)

## API

### `crc16(data: bytes) -> int`
`tools/anchor_c2.py:56`

CRC-16/CCITT-FALSE. Mirrors woz_report_crc16().

**called by** `parse`

### `class BadLine(Exception)`
`tools/anchor_c2.py:73`

The line looked like a report and was not one.

**called by** `parse`

### `parse(line: str) -> dict`
`tools/anchor_c2.py:77`

Parse one ARP1 line. Raises BadLine on anything malformed.

**called by** `handle`  ·  **calls** `BadLine`, `crc16`

### `open_source(path: str, baud: int)`
`tools/anchor_c2.py:118`

A serial port, stdin, or a file, whichever `path` names.

**called by** `main`

<details><summary>Undocumented (2)</summary>

- `main`
- `handle`

</details>
