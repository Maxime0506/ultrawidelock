#!/usr/bin/env python3
"""Anchor C2: read the satellite's ARP1 range-report lines and turn them into CSV.

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
"""

from __future__ import annotations

import argparse
import csv
import sys

MAGIC = "ARP1"
FIELDS = 10  # after the magic, before the "*CRC"

FLAG_VALID = 0x01
FLAG_CALIBRATED = 0x02
FLAG_DEGRADED = 0x04

CSV_HEADER = [
    "seq",
    "anchor",
    "us",
    "d_mm",
    "quality",
    "trust",
    "valid",
    "calibrated",
    "degraded",
    "dropped",
    "accepted",
]


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE. Mirrors woz_report_crc16()."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# The standard's check value. test_woz_report.c pins the C side to the same
# number, which is what makes the two implementations interoperable rather than
# coincidentally similar. If this ever trips, every line would be dropped as
# corrupt and the cause would not be obvious from the symptom.
assert crc16(b"123456789") == 0x29B1, "CRC-16/CCITT-FALSE is wrong"


class BadLine(Exception):
    """The line looked like a report and was not one."""


def parse(line: str) -> dict:
    """Parse one ARP1 line. Raises BadLine on anything malformed."""
    line = line.rstrip("\r\n")
    idx = line.rfind("*")
    if idx < 2 or line[idx - 1] != " " or len(line) - idx != 5:
        raise BadLine("no CRC marker")

    covered = line[: idx - 1]
    try:
        got = int(line[idx + 1 :], 16)
    except ValueError as exc:
        raise BadLine("CRC not hex") from exc
    want = crc16(covered.encode("ascii", "replace"))
    if want != got:
        raise BadLine(f"CRC {got:04X} != {want:04X}")

    parts = covered.split()
    if len(parts) != FIELDS + 1 or parts[0] != MAGIC:
        raise BadLine(f"expected {FIELDS + 1} fields, got {len(parts)}")

    try:
        vals = [int(p) for p in parts[1:]]
    except ValueError as exc:
        raise BadLine("non-numeric field") from exc

    anchor, seq, us_hi, us_lo, d_mm, quality, trust, flags, dropped, accepted = vals
    return {
        "seq": seq,
        "anchor": anchor,
        "us": (us_hi << 32) | us_lo,
        "d_mm": d_mm,
        "quality": quality,
        "trust": trust,
        "valid": int(bool(flags & FLAG_VALID)),
        "calibrated": int(bool(flags & FLAG_CALIBRATED)),
        "degraded": int(bool(flags & FLAG_DEGRADED)),
        "dropped": dropped,
        "accepted": accepted,
    }


def open_source(path: str, baud: int):
    """A serial port, stdin, or a file, whichever `path` names."""
    if path == "-":
        return sys.stdin, None
    if path.startswith("/dev/"):
        try:
            import serial  # type: ignore
        except ImportError:
            sys.exit(
                "reading a serial port needs pyserial: pip install pyserial\n"
                "(or capture with `tio` / `screen` and replay the log file here)"
            )
        port = serial.Serial(path, baud, timeout=1)
        return None, port
    return open(path, "r", encoding="utf-8", errors="replace"), None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("source", help="serial port, log file, or - for stdin")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", help="write rows here as well as counting them")
    ap.add_argument("--quiet", action="store_true", help="summary only")
    args = ap.parse_args()

    stream, port = open_source(args.source, args.baud)

    writer = None
    csv_file = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="", encoding="utf-8")
        writer = csv.DictWriter(csv_file, fieldnames=CSV_HEADER)
        writer.writeheader()

    lines = accepted = crc_fail = malformed = gaps = 0
    last_seq = None

    def handle(raw: str) -> None:
        nonlocal lines, accepted, crc_fail, malformed, gaps, last_seq
        if MAGIC not in raw:
            return
        # A console line may carry a log prefix before the report.
        raw = raw[raw.index(MAGIC) :]
        lines += 1
        try:
            row = parse(raw)
        except BadLine as exc:
            if "CRC" in str(exc):
                crc_fail += 1
            else:
                malformed += 1
            if not args.quiet:
                print(f"  drop: {exc}: {raw.strip()}", file=sys.stderr)
            return

        accepted += 1
        if last_seq is not None and row["seq"] > last_seq + 1:
            gaps += row["seq"] - last_seq - 1
        last_seq = row["seq"]

        if writer:
            writer.writerow(row)
        if not args.quiet:
            print(
                f"seq={row['seq']:<8} d={row['d_mm']:>7} mm  q={row['quality']:<5}"
                f" trust={row['trust']:<3}"
                f" {'cal' if row['calibrated'] else 'RAW'}"
                f"{' DEGRADED' if row['degraded'] else ''}"
            )

    try:
        if port is not None:
            while True:
                handle(port.readline().decode("utf-8", "replace"))
        else:
            for raw in stream:
                handle(raw)
    except KeyboardInterrupt:
        pass
    finally:
        if csv_file:
            csv_file.close()
        if port is not None:
            port.close()

    total = accepted + gaps
    loss = (100.0 * gaps / total) if total else 0.0
    print(
        f"\nlines {lines}  accepted {accepted}  crc-fail {crc_fail}"
        f"  malformed {malformed}  gaps {gaps} ({loss:.1f}%)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
