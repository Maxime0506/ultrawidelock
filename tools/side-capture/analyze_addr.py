#!/usr/bin/env python3
"""Summarise ADDR-filtered side diffs from side_hitl JSONL.

Skips smashed/invalid lines. Optional --phase outside_pause to score only
labelled outside pauses (the scarce class from desk walks).

  python3 tools/side-capture/analyze_addr.py captures/aliro_outpause.jsonl
  python3 tools/side-capture/analyze_addr.py captures/*.jsonl --phase outside_pause
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_rows(paths: list[Path], phase: str | None) -> tuple[list[dict], int]:
    rows: list[dict] = []
    bad = 0
    for path in paths:
        text = path.read_text(errors="replace")
        for line in text.splitlines():
            if not line.strip():
                continue
            try:
                o = json.loads(line)
            except Exception:
                bad += 1
                continue
            if o.get("type") == "phase":
                continue
            an = o.get("anchors") or {}
            if not any(isinstance(v, dict) and v.get("addr") for v in an.values()):
                continue
            if phase and o.get("phase") != phase:
                continue
            d = o.get("diff") or {}
            rows.append(
                {
                    "path": str(path),
                    "walk": o.get("walk"),
                    "phase": o.get("phase"),
                    "omi": d.get("outside_minus_inside"),
                    "n": {k: v.get("n") for k, v in an.items() if isinstance(v, dict)},
                    "mean": {k: v.get("mean") for k, v in an.items() if isinstance(v, dict)},
                }
            )
    return rows, bad


def score(rows: list[dict], *, min_n: int) -> None:
    omis = [r["omi"] for r in rows if r["omi"] is not None]
    q = [
        r
        for r in rows
        if r["omi"] is not None
        and (r["n"].get("outside") or 0) >= min_n
        and (r["n"].get("inside") or 0) >= min_n
    ]
    qomis = [r["omi"] for r in q]
    print(f"filtered={len(rows)}  quality_n>={min_n}={len(q)}")
    if omis:
        print(
            f"all omi: min={min(omis)} max={max(omis)} mean={sum(omis)/len(omis):.1f} "
            f">+3={sum(1 for x in omis if x > 3)} <-3={sum(1 for x in omis if x < -3)}"
        )
    if qomis:
        print(
            f"qual omi: min={min(qomis)} max={max(qomis)} mean={sum(qomis)/len(qomis):.1f} "
            f">+3={sum(1 for x in qomis if x > 3)} <-3={sum(1 for x in qomis if x < -3)}"
        )
    by_phase: dict[str, list[int]] = {}
    for r in q:
        by_phase.setdefault(str(r.get("phase")), []).append(int(r["omi"]))
    if by_phase:
        print("quality by phase:")
        for ph, xs in sorted(by_phase.items()):
            print(
                f"  {ph:16} n={len(xs):3} mean={sum(xs)/len(xs):6.1f} "
                f">+3={sum(1 for x in xs if x > 3)} <-3={sum(1 for x in xs if x < -3)}"
            )
    print("last quality rows:")
    for r in q[-15:]:
        print(
            f"  walk={r.get('walk')} phase={r.get('phase')} "
            f"omi={r['omi']!s:>4} n={r['n']} mean={r['mean']}"
        )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jsonl", nargs="+", type=Path)
    ap.add_argument("--phase", help="only buckets with this phase label")
    ap.add_argument("--min-n", type=int, default=3, help="min n on outside and inside")
    args = ap.parse_args()
    missing = [p for p in args.jsonl if not p.exists()]
    if missing:
        print(f"missing: {missing}", file=sys.stderr)
        return 2
    rows, bad = load_rows(args.jsonl, args.phase)
    print(f"files={len(args.jsonl)} skipped_bad_json={bad} phase_filter={args.phase!r}")
    score(rows, min_n=args.min_n)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
