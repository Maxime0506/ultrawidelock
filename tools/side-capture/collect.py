#!/usr/bin/env python3
"""Collect WR1 witness lines + optional ALAB/SD logs into a labelled trajectory.

Dataset format (JSON Lines, one object per observation window):

  {
    "schema": 1,
    "ts_host": "2026-08-10T01:00:00.000Z",
    "label": "outside_approaching",
    "obs_session_id": 3,
    "anchors": {
      "inside":  {"n": 10, "mean": -70, "var": 12},
      "outside": {"n": 11, "mean": -55, "var": 9},
      "threshold": {"n": 8, "mean": -60, "var": 15}
    },
    "diff": {
      "outside_minus_inside": 15,
      "threshold_minus_inside": 10,
      "outside_minus_threshold": 5
    },
    "uwb_range_mm": 1200,
    "side_decision": null
  }

Split training/test by complete trajectory files, never by shuffling adjacent
windows from one approach.

Usage:
  python3 tools/side-capture/collect.py --label outside_approaching --uart /dev/ttyUSB0
  python3 tools/side-capture/collect.py --replay captures/day1/*.jsonl --baseline
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

WR1_RE = re.compile(
    r"^WR1 role=(?P<role>inside|outside|threshold) obs=(?P<obs>\d+) "
    r"filt=(?P<filt>[0-9a-fA-F]+) n=(?P<n>\d+) mean=(?P<mean>-?\d+) "
    r"min=(?P<min>-?\d+) max=(?P<max>-?\d+) var=(?P<var>-?\d+)\s*$"
)


def parse_wr1(line: str) -> dict | None:
    m = WR1_RE.match(line.strip())
    if not m:
        return None
    return {
        "role": m.group("role"),
        "obs_session_id": int(m.group("obs")),
        "filter": m.group("filt"),
        "n": int(m.group("n")),
        "mean": int(m.group("mean")),
        "min": int(m.group("min")),
        "max": int(m.group("max")),
        "var": int(m.group("var")),
    }


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


def baseline_report(paths: list[Path]) -> int:
    """Measure differential-RSSI separation on labelled trajectories."""
    by_label: dict[str, list[int]] = {}
    for path in paths:
        with path.open() as fh:
            for line in fh:
                if not line.strip():
                    continue
                row = json.loads(line)
                label = row.get("label") or path.stem
                oi = (row.get("diff") or {}).get("outside_minus_inside")
                if oi is None:
                    continue
                by_label.setdefault(label, []).append(int(oi))

    print("differential RSSI baseline (outside_minus_inside, dB)")
    print(f"{'label':28} {'n':>5} {'median':>8} {'p05':>6} {'p95':>6}")
    for label, vals in sorted(by_label.items()):
        vals = sorted(vals)
        if not vals:
            continue
        n = len(vals)
        med = statistics.median(vals)
        p05 = vals[max(0, int(0.05 * (n - 1)))]
        p95 = vals[min(n - 1, int(0.95 * (n - 1)))]
        print(f"{label:28} {n:5d} {med:8.1f} {p05:6d} {p95:6d}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--label", default="unlabelled")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--uart", type=Path, help="serial device emitting WR1 lines")
    ap.add_argument("--replay", nargs="+", type=Path, help="jsonl files")
    ap.add_argument("--baseline", action="store_true")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    if args.baseline:
        paths = args.replay or []
        if not paths:
            print("need --replay files for --baseline", file=sys.stderr)
            return 2
        return baseline_report(paths)

    if args.uart:
        try:
            import serial  # type: ignore
        except ImportError:
            print("pyserial required: pip install pyserial", file=sys.stderr)
            return 2
        out = args.out or Path(
            f"captures/{args.label}_{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}.jsonl"
        )
        out.parent.mkdir(parents=True, exist_ok=True)
        pending: dict[int, dict] = {}
        ser = serial.Serial(str(args.uart), args.baud, timeout=1)
        print(f"collecting from {args.uart} -> {out}", file=sys.stderr)
        with out.open("a") as fh:
            while True:
                raw = ser.readline().decode("utf-8", errors="replace")
                w = parse_wr1(raw)
                if not w:
                    continue
                bucket = pending.setdefault(
                    w["obs_session_id"],
                    {
                        "schema": 1,
                        "ts_host": datetime.now(timezone.utc).isoformat(),
                        "label": args.label,
                        "obs_session_id": w["obs_session_id"],
                        "anchors": {},
                    },
                )
                bucket["anchors"][w["role"]] = {
                    "n": w["n"],
                    "mean": w["mean"],
                    "min": w["min"],
                    "max": w["max"],
                    "var": w["var"],
                }
                if len(bucket["anchors"]) >= 2:
                    bucket["diff"] = diffs(bucket["anchors"])
                    fh.write(json.dumps(bucket, sort_keys=True) + "\n")
                    fh.flush()
                    print(bucket["diff"], file=sys.stderr)
                    del pending[w["obs_session_id"]]
        return 0

    print("specify --uart or --replay/--baseline", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
