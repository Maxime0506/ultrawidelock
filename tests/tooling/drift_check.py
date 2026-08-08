#!/usr/bin/env python3
"""drift_check.py — one value, one meaning, everywhere in the tree.

A constant that a Zephyr Kconfig and a C header both name is written down twice,
because Kconfig cannot include a C header and C cannot read Kconfig. Two copies
of a number is a number that will disagree with itself eventually, and the
disagreement is silent: the host suites compile the C fallback and the firmware
compiles the Kconfig value, so both sides pass while testing different software.

This is the gate that makes that impossible. It re-derives both sides from
structure on every run -- `config <SYM>` / `default <V>` out of the Kconfigs,
`#ifndef CONFIG_<SYM>` / `#define CONFIG_<SYM> <V>` out of the C -- and fails
when two spellings of one constant do not agree.

Deliberately NOT how the old gates worked: nothing here cites a file:line, quotes
prose, or pattern-matches a comment. Move a definition, reflow a file, or add a
hundred lines above it and this still reads the same values. It breaks only when
a value genuinely disagrees, which is the whole point of keeping it.

Three checks:
  1. Kconfig default vs C fallback, per symbol.
  2. The same symbol defined in several Kconfigs, with different defaults.
  3. The same symbol given several different C fallbacks.

Exit 0 when every constant agrees with itself, 1 otherwise.
"""
import os
import re
import sys

# Only these trees hold first-party constants. Vendored code keeps its own, and
# policing a third party's defaults would make this gate fail on their next drop
# rather than on our mistake.
ROOTS = ("modules", "ports", "firmware", "anchor")
SKIP = (
    "modules/woz_dw3000/",          # Qorvo decadriver, verbatim
    "modules/woz_dfu/src/detools/", # detools + heatshrink, verbatim
    "ports/nrf5340dk/patches/",     # diffs against the Nordic add-on
    "workspace/",
    "build/",
)

# `config SYM` opens a block; `default V` inside it, with no `if`, is the value
# we can compare. A conditional default is a deliberate per-board override and
# is skipped rather than guessed at.
CONFIG_OPEN = re.compile(r"^\s*(?:menu)?config\s+([A-Z0-9_]+)\s*$")
DEFAULT = re.compile(r"^\s*default\s+(-?\w+)\s*$")
BLOCK_END = re.compile(r"^\s*(?:config|menuconfig|choice|endchoice|menu|endmenu|if|endif|source)\b")

# The C side: an #ifndef/#define pair guarding a Kconfig symbol. The value may
# be parenthesised, which is style rather than meaning, so it is unwrapped.
IFNDEF = re.compile(r"^\s*#\s*ifndef\s+CONFIG_([A-Z0-9_]+)\s*$")
DEFINE = re.compile(r"^\s*#\s*define\s+CONFIG_([A-Z0-9_]+)\s+(.+?)\s*$")


def walk(exts):
    for root in ROOTS:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in ("build", "workspace")]
            rel = dirpath.replace(os.sep, "/") + "/"
            if any(rel.startswith(s) for s in SKIP):
                continue
            for fn in filenames:
                if fn.endswith(exts) or fn == exts:
                    yield os.path.join(dirpath, fn)


def norm(v):
    """Unwrap parens and normalise an integer so (-55) and -55 compare equal."""
    v = v.strip()
    while v.startswith("(") and v.endswith(")"):
        v = v[1:-1].strip()
    try:
        return str(int(v, 0))
    except ValueError:
        return v


def kconfig_defaults():
    """{symbol: {value: [files]}} for unconditional scalar defaults."""
    out = {}
    for path in walk(("Kconfig",)):
        if os.path.basename(path) != "Kconfig" and not os.path.basename(path).startswith("Kconfig"):
            continue
        sym = None
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = CONFIG_OPEN.match(line)
                if m:
                    sym = m.group(1)
                    continue
                if sym and BLOCK_END.match(line) and not CONFIG_OPEN.match(line):
                    sym = None
                    continue
                if not sym:
                    continue
                d = DEFAULT.match(line)
                if d:
                    out.setdefault(sym, {}).setdefault(norm(d.group(1)), []).append(path)
                    sym = None
    return out


def c_fallbacks():
    """{symbol: {value: [files]}} for #ifndef CONFIG_X / #define CONFIG_X V."""
    out = {}
    for path in walk((".c", ".h", ".cpp")):
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        for i, line in enumerate(lines):
            m = IFNDEF.match(line)
            if not m or i + 1 >= len(lines):
                continue
            d = DEFINE.match(lines[i + 1])
            if d and d.group(1) == m.group(1):
                out.setdefault(m.group(1), {}).setdefault(norm(d.group(2)), []).append(path)
    return out


def ok(msg):
    """One passing check, in the row format scripts/test-runner.sh counts."""
    print(f"  ok   {msg}")


def fail(msg):
    print(f"  FAIL {msg}")


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    kc = kconfig_defaults()
    cf = c_fallbacks()
    bad = 0

    print("== constant drift · Kconfig vs C ==")

    # 1. One symbol, several Kconfig defaults.
    for sym, vals in sorted(kc.items()):
        if len(vals) > 1:
            where = "; ".join(f"{v} in {', '.join(sorted(set(f)))}" for v, f in sorted(vals.items()))
            fail(f"{sym}: Kconfigs disagree — {where}")
            bad += 1

    # 2. One symbol, several C fallbacks.
    for sym, vals in sorted(cf.items()):
        if len(vals) > 1:
            where = "; ".join(f"{v} in {', '.join(sorted(set(f)))}" for v, f in sorted(vals.items()))
            fail(f"{sym}: C fallbacks disagree — {where}")
            bad += 1

    # 3. Kconfig default vs C fallback, the pair that actually diverges in
    #    practice: one side ships in the firmware, the other in the host suites.
    for sym in sorted(set(kc) & set(cf)):
        kvals, cvals = set(kc[sym]), set(cf[sym])
        if kvals == cvals:
            ok(f"{sym} = {next(iter(kvals))}  (Kconfig and C agree)")
        else:
            fail(
                f"{sym}: Kconfig default {'/'.join(sorted(kvals))} != "
                f"C fallback {'/'.join(sorted(cvals))} "
                f"({', '.join(sorted(set(sum(cf[sym].values(), []))))})"
            )
            bad += 1

    # Symbols with only one spelling cannot drift, but the count is worth
    # printing: a sudden drop means the parser stopped seeing definitions.
    print(
        f"\n  scanned {len(kc)} Kconfig default(s), {len(cf)} C fallback(s), "
        f"{len(set(kc) & set(cf))} paired"
    )
    if bad:
        print(f"  RESULT: FAIL — {bad} constant(s) disagree with themselves")
        print("  Fix the value, not the check: one constant, one number.\n")
        return 1
    print("  RESULT: PASS\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
