#!/usr/bin/env python3
"""Build the UltraWideLock site into web/dist/.

Two rules, both learned from the generator this replaces:

  1. Nothing generated is ever committed. The old pipeline wrote a page per
     source file into docs/ and committed them; every merge then conflicted on
     derived line numbers, which no resolution could settle and only a
     regeneration could fix. web/dist/ is gitignored and disposable.

  2. No tool outside this repository. The old pipeline looked for a page
     generator on a machine-local path, so a fresh clone could not build the
     site at all. This is stdlib-only Python and needs nothing installed.

Usage:
    python3 web/build.py            # build into web/dist/
    python3 web/build.py --check    # build, then fail on any dead internal link
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parent
ROOT = WEB.parent
DIST = WEB / "dist"

# Copied verbatim into the output. Source directory -> published path,
# where "" is the site root.
TREES = {
    WEB / "site": "",
    WEB / "assets" / "design": "assets/design",
    WEB / "flasher": "flash",
    WEB / "twin": "twin",
}

# Files inside those trees that are build-time or test-time only.
NOT_PUBLISHED = {".py", ".cjs", ".c", ".sh"}


def clean() -> None:
    if DIST.exists():
        shutil.rmtree(DIST)
    DIST.mkdir(parents=True)


def build_twin_wasm() -> bool:
    """Compile the twin's firmware to WASM. Needs emscripten; skipping is fine.

    The page degrades to an explanation rather than a broken simulator, so a
    contributor without emcc can still build and read the whole site.
    """
    script = WEB / "twin" / "build-wasm.sh"
    if shutil.which("emcc") is None:
        print("build: emcc not found, twin simulator will be absent "
              "(brew install emscripten)")
        return False
    result = subprocess.run(["bash", str(script)], capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip()[-2000:], file=sys.stderr)
        raise SystemExit("build: twin WASM failed")
    print(f"build: {result.stdout.strip()}")
    return True


def copy_trees() -> list[Path]:
    written: list[Path] = []
    for src, rel in TREES.items():
        if not src.is_dir():
            raise SystemExit(f"build: missing source tree {src}")
        for path in sorted(src.rglob("*")):
            if not path.is_file() or path.suffix in NOT_PUBLISHED:
                continue
            if path.name.startswith("_ref-"):
                continue
            out = DIST / rel / path.relative_to(src)
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out)
            written.append(out)
    return written


LINK_RE = re.compile(r'(?:href|src)="([^"#][^"]*)"')


def check_links(pages: list[Path], have_twin: bool) -> list[str]:
    """Every relative href/src in the output must resolve to a real file.

    twin.js is the one exception, and only when emscripten was absent: the
    twin page is still worth publishing without its simulator.
    """
    dead: list[str] = []
    for page in pages:
        if page.suffix != ".html":
            continue
        text = page.read_text(encoding="utf-8", errors="replace")
        for target in LINK_RE.findall(text):
            if target.startswith(("http://", "https://", "mailto:", "data:", "//")):
                continue
            resolved = (page.parent / target.split("?")[0].split("#")[0]).resolve()
            if resolved.is_dir():
                resolved = resolved / "index.html"
            if not resolved.exists():
                if not have_twin and resolved.name == "twin.js":
                    continue
                dead.append(f"{page.relative_to(DIST)} -> {target}")
    return dead


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="fail on any dead internal link")
    args = ap.parse_args()

    clean()
    have_twin = build_twin_wasm()
    written = copy_trees()
    print(f"build: {len(written)} file(s) -> {DIST.relative_to(ROOT)}")

    dead = check_links(written, have_twin)
    if dead:
        for entry in dead:
            print(f"build: dead link  {entry}", file=sys.stderr)
        if args.check:
            print(f"build: {len(dead)} dead link(s)", file=sys.stderr)
            return 1
    elif args.check:
        print("build: no dead internal links")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
