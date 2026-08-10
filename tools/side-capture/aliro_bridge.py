#!/usr/bin/env python3
"""Bridge CDK Aliro SIDE peer= lines to witness ADDR filters.

The DWM3001CDK console is SEGGER RTT (make monitor), NOT a Pi ttyACM.
USB CDC on the CDK is provisioning-only. So the working lab path is:

  Mac:  make monitor | python3 tools/side-capture/aliro_bridge.py --stdin \\
            --ssh <user>@<pi-host> --witness /dev/ttyACM0 /dev/ttyACM1 /dev/ttyACM2
  Pi:   watch_trio.py on the three dongles

On Aliro L2CAP CoC open the lock prints:
  SIDE peer=AA:BB:CC:DD:EE:FF type=random
This pushes ADDR to every witness; SIDE peer=clear clears them.

Legacy (wrong for CDK RTT console):
  python3 aliro_bridge.py --lock /dev/ttyACM3
"""

from __future__ import annotations

import argparse
import glob
import re
import subprocess
import sys
import time

SIDE_PEER = re.compile(
    r"SIDE peer=([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\s+type=\w+",
    re.I,
)
SIDE_CLEAR = re.compile(r"SIDE peer=clear", re.I)
ADDR_OK = re.compile(r"ADDR ok", re.I)

REMOTE_PUSH = r"""
import serial, sys, time, re
line = sys.argv[1]
ports = sys.argv[2:]
ok_re = re.compile(r"ADDR ok", re.I)
for path in ports:
    ok = False
    last_err = None
    for attempt in range(5):
        try:
            ser = serial.Serial(path, 115200, timeout=0.2)
        except Exception as e:
            last_err = e
            time.sleep(0.15)
            continue
        try:
            ser.write((line + "\n").encode("ascii"))
            ser.flush()
            t0 = time.time()
            buf = ""
            while time.time() - t0 < 1.5:
                chunk = ser.read(256).decode("utf-8", errors="replace")
                if not chunk:
                    continue
                buf += chunk
                if ok_re.search(buf):
                    ok = True
                    break
            print(f"{path}: {line} -> {'ok' if ok else 'no ack (try %d)' % (attempt + 1)}", flush=True)
            if ok:
                break
        finally:
            ser.close()
        time.sleep(0.15)
    if not ok and last_err is not None:
        print(f"! {path}: {last_err}", flush=True)
"""


def discover_acm() -> list[str]:
    return sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))


def open_ser(path: str, baud: int = 115200):
    import serial

    return serial.Serial(path, baud, timeout=0.2)


def send_addr_local(ports: list[str], addr: str | None) -> None:
    line = "ADDR 0" if not addr else f"ADDR {addr}"
    for path in ports:
        ok = False
        last_err = None
        for attempt in range(5):
            try:
                ser = open_ser(path)
            except Exception as e:
                last_err = e
                time.sleep(0.15)
                continue
            try:
                # Don't reset_input_buffer — watch_trio may be reading the same ACM.
                ser.write((line + "\n").encode("ascii"))
                ser.flush()
                t0 = time.time()
                buf = ""
                while time.time() - t0 < 1.5:
                    chunk = ser.read(256).decode("utf-8", errors="replace")
                    if not chunk:
                        continue
                    buf += chunk
                    if ADDR_OK.search(buf) or "ADDR ok clear" in buf:
                        ok = True
                        break
                if ok:
                    print(f"{path}: {line} -> ok", flush=True)
                    break
                print(f"{path}: {line} -> no ack (try {attempt + 1})", flush=True)
            finally:
                try:
                    ser.close()
                except Exception:
                    pass
            time.sleep(0.15)
        if not ok and last_err is not None:
            print(f"! {path}: open failed: {last_err}", file=sys.stderr, flush=True)


def send_addr_ssh(host: str, ports: list[str], addr: str | None) -> None:
    line = "ADDR 0" if not addr else f"ADDR {addr}"
    cmd = ["ssh", host, "python3", "-c", REMOTE_PUSH, line, *ports]
    try:
        subprocess.run(cmd, check=False)
    except Exception as e:
        print(f"! ssh {host}: {e}", file=sys.stderr, flush=True)


def handle_stream(lines, witnesses: list[str], ssh_host: str | None) -> None:
    current: str | None = None
    send = (
        (lambda a: send_addr_ssh(ssh_host, witnesses, a))
        if ssh_host
        else (lambda a: send_addr_local(witnesses, a))
    )
    buf = ""
    try:
        for chunk in lines:
            if not chunk:
                continue
            if isinstance(chunk, bytes):
                chunk = chunk.decode("utf-8", errors="replace")
            sys.stdout.write(chunk)
            sys.stdout.flush()
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                if SIDE_CLEAR.search(line):
                    if current is not None:
                        print(">> clear ADDR on witnesses", flush=True)
                        send(None)
                        current = None
                    continue
                m = SIDE_PEER.search(line)
                if m:
                    peer = m.group(1).upper()
                    if peer != current:
                        print(f">> ADDR {peer} -> witnesses", flush=True)
                        send(peer)
                        current = peer
    except KeyboardInterrupt:
        print("\nstopped", flush=True)
        if current is not None:
            send(None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument(
        "--stdin",
        action="store_true",
        help="read SIDE lines from stdin (pipe make monitor here)",
    )
    src.add_argument(
        "--lock",
        help="local serial device (usually wrong for CDK — console is RTT)",
    )
    ap.add_argument(
        "--witness",
        nargs="*",
        help="witness serial devices on the machine that talks USB "
        "(with --ssh: paths on the remote Pi)",
    )
    ap.add_argument(
        "--ssh",
        metavar="HOST",
        help="push ADDR over ssh to HOST (e.g. user@pi-host)",
    )
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    if args.witness:
        witnesses = list(args.witness)
    elif args.ssh:
        print("with --ssh you must pass --witness /dev/ttyACM… on the Pi", file=sys.stderr)
        return 2
    else:
        witnesses = [p for p in discover_acm() if p != args.lock]
    if not witnesses:
        print("no witness ports found", file=sys.stderr)
        return 2

    print(f"witnesses={' '.join(witnesses)}", flush=True)
    if args.ssh:
        print(f"ssh={args.ssh}", flush=True)
    print("waiting for SIDE peer=… (Aliro BLE CoC / Wallet UWB unlock)", flush=True)

    if args.stdin:
        handle_stream(sys.stdin, witnesses, args.ssh)
        return 0

    print(f"lock={args.lock} (UART — CDK app console is RTT; prefer --stdin)", flush=True)
    lock = open_ser(args.lock, args.baud)

    def gen():
        try:
            while True:
                chunk = lock.read(256).decode("utf-8", errors="replace")
                if chunk:
                    yield chunk
        finally:
            lock.close()

    handle_stream(gen(), witnesses, args.ssh)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
