#!/usr/bin/env python3
"""Pretty terminal coverage table from an llvm-cov `-summary-only` JSON export.

Usage: coverage_report.py <summary.json> [html_index_path] [unbuilt_tsv] [surfaces_tsv]

Reads the summary llvm-cov emits, prints one aligned, colourised row per file
(lines covered, line %, function %, and a bar) plus a TOTAL, then the path to
the browsable HTML report. Colour is suppressed when stdout is not a TTY or
NO_COLOR is set, so it stays clean in logs and CI. Files llvm reports with no
countable lines (declaration-only headers) are dropped from the table.

When coverage.sh passes an unbuilt_tsv (repo-relative path + tag per line), a
second table lists every source that never enters a host build as a 0% row —
its line count is a comment-stripped SLOC estimate, not llvm region lines —
and the closing "all our C code" total folds those lines into the denominator.
Candidates llvm-cov already measured are dropped here, so headers appear in
exactly one table. CI's floor reads summary.json, not that total.

A surfaces_tsv (display name, line count, status) adds a third block: testing
surfaces outside the C domain (python, web, patches, shell), listed without
percentages because nothing instruments them.
"""

import json
import os
import re
import sys

TAG_LABEL = {
    "untested": "no host tests yet",
    "target-only": "target-only (HW/SDK)",
}


def sloc(path: str) -> int:
    """Comment- and blank-stripped line count (estimate, not llvm regions)."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return sum(
        1 for line in text.splitlines() if re.sub(r"//.*", "", line).strip()
    )


def shortname(rel: str) -> str:
    """Trim scaffolding path segments so rows stay readable."""
    parts = rel.split("/")
    if parts and parts[0] in ("modules", "ports"):
        parts = parts[1:]
    return "/".join(
        p for p in parts if p not in ("src", "apps", "main", "components", "include")
    )


def main() -> int:
    summary_path = sys.argv[1]
    html_path = sys.argv[2] if len(sys.argv) > 2 else None
    unbuilt_path = sys.argv[3] if len(sys.argv) > 3 else None
    surfaces_path = sys.argv[4] if len(sys.argv) > 4 else None

    with open(summary_path) as fh:
        doc = json.load(fh)
    # 0-count entries are declaration-only headers: nothing measurable there.
    files = [f for f in doc["data"][0]["files"] if f["summary"]["lines"]["count"]]
    totals = doc["data"][0]["totals"]

    root = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    measured = {os.path.realpath(f["filename"]) for f in files}
    unbuilt = []  # (display name, sloc, tag)
    if unbuilt_path:
        with open(unbuilt_path) as fh:
            for line in fh:
                rel, tag = line.rstrip("\n").split("\t")
                if os.path.realpath(os.path.join(root, rel)) in measured:
                    continue  # llvm already counts it in the table above
                unbuilt.append((shortname(rel), sloc(os.path.join(root, rel)), tag))
        # actionable rows first, then the target-bound tail; alpha within each
        unbuilt.sort(key=lambda r: (r[2] != "untested", r[0]))

    surfaces = []  # (display name, line count, status)
    if surfaces_path:
        with open(surfaces_path) as fh:
            for line in fh:
                name, count, status = line.rstrip("\n").split("\t")
                surfaces.append((name, count, status))

    color = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
    if color:
        B, C, Y, D, R = "\033[1m", "\033[36m", "\033[1;33m", "\033[2m", "\033[0m"
        GRN, YEL, RED = "\033[32m", "\033[33m", "\033[31m"
    else:
        B = C = Y = D = R = GRN = YEL = RED = ""

    def tint(pct: float) -> str:
        return GRN if pct >= 90 else YEL if pct >= 75 else RED

    def bar(pct: float, width: int = 12) -> str:
        filled = int(round(pct / 100.0 * width))
        return "█" * filled + "░" * (width - filled)

    names = [os.path.basename(f["filename"]) for f in files]
    namew = max([len("file")] + [len(n) for n in names])

    def frac(cov: int, tot: int) -> str:
        return f"{cov}/{tot}"

    # widest "cov/tot" so the fraction column aligns.
    all_lines = [f["summary"]["lines"] for f in files] + [totals["lines"]]
    fracw = max(
        len("lines"), max(len(frac(l["covered"], l["count"])) for l in all_lines)
    )

    def row(label, width, lines, funcs, bold=False):
        pct = lines["percent"]
        t = tint(pct)
        lb = f"{B}{label}{R}" if bold else f"{C}{label}{R}"
        pctstr = f"{t}{B if bold else ''}{pct:5.1f}%{R}"
        return (
            f"    {lb}{' ' * (width - len(label))}   "
            f"{frac(lines['covered'], lines['count']):>{fracw}}   "
            f"{pctstr}   {funcs['percent']:5.1f}%   {t}{bar(pct)}{R}"
        )

    print()
    print(f"  {B}Coverage{R}  {D}·  our code — host suites (instrumented){R}")
    print()
    print(
        f"    {D}{'file':<{namew}}   {'lines':>{fracw}}   {'cover':>6}   "
        f"{'funcs':>6}   {'':<12}{R}"
    )
    for f, n in zip(files, names):
        s = f["summary"]
        print(row(n, namew, s["lines"], s["functions"]))
    print(f"    {D}{'─' * (namew + fracw + 36)}{R}")
    print(row("TOTAL host-tested", max(namew, 17), totals["lines"], totals["functions"], bold=True))

    if unbuilt:
        cov = totals["lines"]["covered"]
        cnt = totals["lines"]["count"] + sum(n for _, n, _ in unbuilt)
        pct = 100.0 * cov / cnt if cnt else 0.0
        uw = max(len("TOTAL all our C code"), max(len(n) for n, _, _ in unbuilt))
        ufracw = max(
            [len("lines"), len(frac(cov, cnt))]
            + [len(frac(0, n)) for _, n, _ in unbuilt]
        )
        print()
        print(
            f"  {B}Never built on host{R}  "
            f"{D}·  0% — line counts are SLOC estimates{R}"
        )
        print()
        print(
            f"    {D}{'file':<{uw}}   {'lines':>{ufracw}}   {'cover':>6}   "
            f"{'why':<21}{R}"
        )
        for name, n, tag in unbuilt:
            print(
                f"    {C}{name}{R}{' ' * (uw - len(name))}   "
                f"{frac(0, n):>{ufracw}}   {RED}  0.0%{R}   "
                f"{D}{TAG_LABEL.get(tag, tag)}{R}"
            )
        t = tint(pct)
        label = "TOTAL all our C code"
        print(f"    {D}{'─' * (uw + ufracw + 36)}{R}")
        print(
            f"    {B}{label}{R}{' ' * (uw - len(label))}   "
            f"{frac(cov, cnt):>{ufracw}}   {t}{B}{pct:5.1f}%{R}   "
            f"{t}{bar(pct)}{R}"
        )

    if surfaces:
        sw = max(len("surface"), max(len(n) for n, _, _ in surfaces))
        slw = max(len("lines"), max(len(c) for _, c, _ in surfaces))
        print()
        print(
            f"  {B}Beyond C{R}  "
            f"{D}·  testing surfaces nothing instruments{R}"
        )
        print()
        print(f"    {D}{'surface':<{sw}}   {'lines':>{slw}}   status{R}")
        for name, count, status in surfaces:
            print(
                f"    {C}{name}{R}{' ' * (sw - len(name))}   "
                f"{count:>{slw}}   {D}{status}{R}"
            )

    print()
    if html_path:
        print(f"    {D}html:{R} {html_path}")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
