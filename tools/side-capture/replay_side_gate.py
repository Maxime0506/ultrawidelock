#!/usr/bin/env python3
"""Replay pose JSONL through the fail-closed side gate (mirrors woz_side defaults).

Defaults match modules/woz_anchor/src/woz_side.c:woz_side_defaults:
  margin=6 dB, min_pkts=3, agree_windows=3, confidence_min=70

  python3 tools/side-capture/replay_side_gate.py \\
      captures/pose_out.jsonl captures/pose_in.jsonl
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

MARGIN = 6
MIN_PKTS = 3
AGREE = 3
CONF_MIN = 70

OUTSIDE, INSIDE, THRESHOLD, UNKNOWN = "OUTSIDE", "INSIDE", "THRESHOLD", "UNKNOWN"


@dataclass
class Filter:
    cand: str = UNKNOWN
    cand_n: int = 0
    committed: str = UNKNOWN


def classify(inside: int, outside: int, threshold: int | None, ni: int, no: int, nt: int) -> tuple[str, int, int]:
    """Return (side, confidence, omi)."""
    if ni < MIN_PKTS or no < MIN_PKTS:
        return UNKNOWN, 0, 0
    omi = outside - inside  # dBm: less-negative outside => positive omi
    side = UNKNOWN
    conf = 0
    if omi >= MARGIN:
        side = OUTSIDE
        conf = min(100, 60 + (omi - MARGIN) * 4)
    elif omi <= -MARGIN:
        side = INSIDE
        conf = min(100, 60 + (-omi - MARGIN) * 4)
    elif threshold is not None and nt >= MIN_PKTS:
        # weak band → threshold-ish
        side = THRESHOLD
        conf = 55
    return side, conf, omi


def feed(filt: Filter, side: str, conf: int) -> tuple[str, bool]:
    if side == UNKNOWN:
        filt.cand = UNKNOWN
        filt.cand_n = 0
        return filt.committed, False
    if side == filt.cand:
        filt.cand_n += 1
    else:
        filt.cand = side
        filt.cand_n = 1
    if filt.cand_n >= AGREE:
        # no direct INSIDE<->OUTSIDE without going through unknown in this simplified mirror
        if filt.committed == INSIDE and side == OUTSIDE and filt.cand_n < AGREE + 2:
            return filt.committed, False
        filt.committed = side
    unlock = filt.committed == OUTSIDE and conf >= CONF_MIN and filt.cand_n >= AGREE
    return filt.committed, unlock


def load_addr_buckets(path: Path, phase: str | None) -> list[dict]:
    rows = []
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        if o.get("type") == "phase":
            continue
        an = o.get("anchors") or {}
        if not any(isinstance(v, dict) and v.get("addr") for v in an.values()):
            continue
        if phase and o.get("phase") != phase:
            continue
        rows.append(o)
    return rows


def replay(path: Path, phase: str | None) -> None:
    rows = load_addr_buckets(path, phase)
    filt = Filter()
    unlocks = 0
    sides = {OUTSIDE: 0, INSIDE: 0, THRESHOLD: 0, UNKNOWN: 0}
    quality = 0
    for o in rows:
        an = o.get("anchors") or {}
        inn = an.get("inside") or {}
        out = an.get("outside") or {}
        thr = an.get("threshold") or {}
        ni, no, nt = int(inn.get("n") or 0), int(out.get("n") or 0), int(thr.get("n") or 0)
        if ni < MIN_PKTS or no < MIN_PKTS:
            continue
        quality += 1
        side, conf, omi = classify(
            int(inn["mean"]),
            int(out["mean"]),
            int(thr["mean"]) if thr.get("mean") is not None and nt else None,
            ni,
            no,
            nt,
        )
        committed, unlock = feed(filt, side, conf)
        sides[side] = sides.get(side, 0) + 1
        if unlock:
            unlocks += 1
    print(f"{path.name} phase={phase!r}")
    print(f"  addr_buckets={len(rows)} quality={quality}")
    print(f"  raw_sides={sides}")
    print(f"  final_committed={filt.committed} unlock_windows={unlocks}/{quality}")
    if phase == "outside_pause":
        ok = unlocks > 0 and filt.committed == OUTSIDE
        print(f"  expect OUTSIDE unlocks: {'PASS' if ok else 'FAIL'}")
    if phase == "inside_pause":
        ok = unlocks == 0 and filt.committed != OUTSIDE
        print(f"  expect no OUTSIDE unlock: {'PASS' if ok else 'FAIL'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jsonl", nargs="+", type=Path)
    ap.add_argument("--phase", help="restrict to phase (default: each file's natural phase if present)")
    args = ap.parse_args()
    for path in args.jsonl:
        if not path.exists():
            print(f"missing {path}", file=sys.stderr)
            return 2
        phase = args.phase
        if phase is None:
            # sniff dominant phase among addr buckets
            from collections import Counter

            c = Counter()
            for o in load_addr_buckets(path, None):
                c[o.get("phase") or "walking"] += 1
            phase = c.most_common(1)[0][0] if c else None
        replay(path, phase)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
