#!/usr/bin/env python3
"""LEARN a BLE adv fingerprint on one witness, then FILT all plugged dongles.

Usage on the Pi (phone held against the OUT dongle during LEARN):
  python3 set_filt.py --learn /dev/ttyACM1
  python3 set_filt.py --filt a1b2
  python3 set_filt.py --clear
"""

from __future__ import annotations

import argparse
import glob
import re
import sys
import time

LEARN_OK = re.compile(r"LEARN ok filt=([0-9a-fA-F]{4})", re.I)
FILT_OK = re.compile(r"FILT ok filt=([0-9a-fA-F]{4})", re.I)


def ports() -> list[str]:
    return sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))


def open_ser(path: str, baud: int = 115200):
    import serial

    return serial.Serial(path, baud, timeout=0.2)


def drain(ser, seconds: float = 0.3) -> None:
    t0 = time.time()
    while time.time() - t0 < seconds:
        ser.read(256)


def send_line(ser, line: str) -> None:
    ser.write((line.strip() + "\n").encode("ascii"))
    ser.flush()


def learn(port: str, wait_s: float = 8.0) -> str:
    ser = open_ser(port)
    try:
        drain(ser, 0.5)
        send_line(ser, "LEARN")
        print(f"LEARN on {port} — hold phone against that dongle…", flush=True)
        t0 = time.time()
        buf = ""
        while time.time() - t0 < wait_s:
            chunk = ser.read(256).decode("utf-8", errors="replace")
            if not chunk:
                continue
            sys.stdout.write(chunk)
            sys.stdout.flush()
            buf += chunk
            m = LEARN_OK.search(buf)
            if m:
                return m.group(1).lower()
            if "LEARN fail" in buf:
                raise SystemExit("LEARN fail — no fingerprint (move phone closer, retry)")
        raise SystemExit("LEARN timeout — no LEARN ok line")
    finally:
        ser.close()


def set_filt_all(filt_hex: str, targets: list[str] | None = None) -> None:
    filt_hex = filt_hex.lower().replace("0x", "")
    for path in targets or ports():
        ser = open_ser(path)
        try:
            drain(ser, 0.2)
            send_line(ser, f"FILT {filt_hex}")
            t0 = time.time()
            buf = ""
            ok = False
            while time.time() - t0 < 2.0:
                chunk = ser.read(256).decode("utf-8", errors="replace")
                if not chunk:
                    continue
                buf += chunk
                if FILT_OK.search(buf):
                    ok = True
                    break
            print(f"{path}: {'FILT ok' if ok else 'no ack'} filt={filt_hex}", flush=True)
        finally:
            ser.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--learn", metavar="PORT", help="run LEARN on this port, then FILT all")
    ap.add_argument("--filt", metavar="HEX", help="set FILT on all ttyACM*/ttyUSB*")
    ap.add_argument("--clear", action="store_true", help="FILT 0 on all ports")
    ap.add_argument("--ports", nargs="*", help="limit FILT targets")
    args = ap.parse_args()

    if args.clear:
        set_filt_all("0", args.ports)
        return 0
    if args.filt:
        set_filt_all(args.filt, args.ports)
        return 0
    if args.learn:
        filt = learn(args.learn)
        print(f"broadcast FILT {filt} to all ports", flush=True)
        set_filt_all(filt, args.ports)
        print(f"done filt={filt} — now run watch_trio / capture", flush=True)
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
