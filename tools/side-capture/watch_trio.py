#!/usr/bin/env python3
"""Read WR1 lines from all plugged nRF52840 witness dongles at once.

Each dongle has its own obs counter, so capture buckets by host time
(~3 s windows), not obs_session_id.

Usage on the Pi:
  python3 watch_trio.py
  python3 watch_trio.py --ports /dev/ttyACM0 /dev/ttyACM1 /dev/ttyACM2
  python3 watch_trio.py --label outside_approaching --out captures/run.jsonl

Stop with Ctrl-C (hard-exits within ~0.5s). If it still wedges:
  pkill -f watch_trio.py
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import signal
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

WR1_RE = re.compile(
    r"WR1 role=(?P<role>inside|outside|threshold) obs=(?P<obs>\d+) "
    r"filt=(?P<filt>[0-9a-fA-F]+) "
    r"(?:addr=(?P<addr>[0-9a-fA-F]{12}) )?"
    r"n=(?P<n>\d+) mean=(?P<mean>-?\d+) "
    r"min=(?P<min>-?\d+) max=(?P<max>-?\d+) var=(?P<var>-?\d+)"
)

WINDOW_S = 3.0


def discover_ports() -> list[Path]:
    ports = sorted(Path("/dev").glob("ttyACM*"))
    ports += sorted(Path("/dev").glob("ttyUSB*"))
    return ports


def parse_wr1(line: str) -> dict | None:
    m = WR1_RE.search(line)
    if not m:
        return None
    d = {
        "role": m.group("role"),
        "obs_session_id": int(m.group("obs")),
        "filter": m.group("filt"),
        "n": int(m.group("n")),
        "mean": int(m.group("mean")),
        "min": int(m.group("min")),
        "max": int(m.group("max")),
        "var": int(m.group("var")),
    }
    if m.group("addr"):
        d["addr"] = m.group("addr")
    return d


def diffs(anchors: dict) -> dict:
    inn = anchors.get("inside", {}).get("mean")
    out = anchors.get("outside", {}).get("mean")
    thr = anchors.get("threshold", {}).get("mean")
    d = {}
    if inn is not None and out is not None:
        d["outside_minus_inside"] = out - inn
    if inn is not None and thr is not None:
        d["threshold_minus_inside"] = thr - inn
    if out is not None and thr is not None:
        d["outside_minus_threshold"] = out - thr
    return d


def reader(
    port: Path,
    baud: int,
    q: queue.Queue,
    stop: threading.Event,
    ports_lock: threading.Lock,
    open_ports: dict,
) -> None:
    try:
        import serial
    except ImportError:
        q.put(("err", port, "pyserial missing: sudo apt install python3-serial"))
        return
    try:
        # Non-exclusive: aliro_bridge must briefly open the same ACM to push ADDR.
        ser = serial.Serial(str(port), baud, timeout=0.2)
    except Exception as e:
        q.put(("err", port, str(e)))
        return
    with ports_lock:
        open_ports[str(port)] = ser
    q.put(("info", port, "open"))
    buf = ""
    try:
        while not stop.is_set():
            try:
                chunk = ser.read(256).decode("utf-8", errors="replace")
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                w = parse_wr1(line)
                if w:
                    w["port"] = str(port)
                    w["ts"] = time.time()
                    q.put(("wr1", port, w))
    finally:
        try:
            ser.close()
        except Exception:
            pass
        with ports_lock:
            open_ports.pop(str(port), None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ports", nargs="*", type=Path, help="serial devices (default: all ttyACM*/ttyUSB*)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--label", default="unlabelled")
    ap.add_argument("--out", type=Path, help="optional JSONL capture path")
    args = ap.parse_args()

    ports = args.ports or discover_ports()
    if not ports:
        print("no /dev/ttyACM* or /dev/ttyUSB* found — plug the three dongles in", file=sys.stderr)
        return 2

    print(f"watching {len(ports)} port(s): {' '.join(str(p) for p in ports)}", file=sys.stderr)
    print("Ctrl-C to stop (hard exit ~0.5s)", file=sys.stderr)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        print(f"writing JSONL -> {args.out}", file=sys.stderr)

    q: queue.Queue = queue.Queue()
    stop = threading.Event()
    ports_lock = threading.Lock()
    open_ports: dict = {}
    role_port: dict[str, str] = {}
    buckets: dict[int, dict] = {}
    out_fh = args.out.open("a") if args.out else None
    exiting = threading.Event()

    def close_ports() -> None:
        with ports_lock:
            for ser in list(open_ports.values()):
                try:
                    ser.cancel_read()
                except Exception:
                    pass
                try:
                    ser.close()
                except Exception:
                    pass
            open_ports.clear()

    def hard_exit(code: int = 130) -> None:
        if out_fh:
            try:
                out_fh.close()
            except Exception:
                pass
        if role_port:
            print("\nstopped — role → port map:", file=sys.stderr)
            for role, p in sorted(role_port.items()):
                print(f"  {role:9} {p}", file=sys.stderr)
        sys.stderr.flush()
        sys.stdout.flush()
        os._exit(code)

    def shutdown(_signum=None, _frame=None) -> None:
        # Second Ctrl-C: kill immediately.
        if exiting.is_set():
            os._exit(130)
        exiting.set()
        stop.set()
        close_ports()
        # pyserial/USB on Pi can wedge join/cleanup — force death soon.
        threading.Timer(0.4, hard_exit).start()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    threads = [
        threading.Thread(
            target=reader,
            args=(p, args.baud, q, stop, ports_lock, open_ports),
            daemon=True,
        )
        for p in ports
    ]
    for t in threads:
        t.start()

    try:
        while not stop.is_set():
            try:
                kind, port, payload = q.get(timeout=0.2)
            except queue.Empty:
                continue
            if kind == "err":
                print(f"! {port}: {payload}", file=sys.stderr)
                continue
            if kind == "info":
                print(f"+ {port}: {payload}", file=sys.stderr)
                continue

            w = payload
            role_port[w["role"]] = w["port"]
            addr = w.get("addr")
            addr_s = f" addr={addr}" if addr else ""
            print(
                f"{w['port']}  WR1 role={w['role']:9} obs={w['obs_session_id']:<5} "
                f"n={w['n']:<3} mean={w['mean']:>4} var={w['var']}{addr_s}",
                flush=True,
            )

            if out_fh is None:
                continue

            key = int(w["ts"] // WINDOW_S)
            bucket = buckets.setdefault(
                key,
                {
                    "schema": 1,
                    "ts_host": datetime.now(timezone.utc).isoformat(),
                    "label": args.label,
                    "obs_session_id": key,
                    "anchors": {},
                    "ports": {},
                },
            )
            bucket["anchors"][w["role"]] = {
                "n": w["n"],
                "mean": w["mean"],
                "min": w["min"],
                "max": w["max"],
                "var": w["var"],
            }
            if addr:
                bucket["anchors"][w["role"]]["addr"] = addr
            bucket["ports"][w["role"]] = w["port"]

            stale = [k for k in buckets if k < key - 1]
            for k in stale + ([key] if len(bucket["anchors"]) >= 3 else []):
                b = buckets.get(k)
                if not b or len(b["anchors"]) < 2:
                    if k in stale:
                        buckets.pop(k, None)
                    continue
                b["diff"] = diffs(b["anchors"])
                out_fh.write(json.dumps(b, sort_keys=True) + "\n")
                out_fh.flush()
                print(f"  -> saved {b['diff']}", file=sys.stderr, flush=True)
                buckets.pop(k, None)
    finally:
        stop.set()
        close_ports()
        hard_exit(0)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
