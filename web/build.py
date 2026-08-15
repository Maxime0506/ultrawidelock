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


def build_graph() -> Path | None:
    """Render the subsystem graph, when graphify has produced data to render.

    graphify is not in this repository and graphify-out/ is gitignored, so the
    graph is an enrichment and never a build requirement. Refresh it with:

        graphify . --update --code-only
    """
    source = ROOT / "graphify-out" / "graph.json"
    if not source.is_file():
        print("build: graphify-out/graph.json absent, skipping the graph "
              "(graphify . --update --code-only)")
        return None
    sys.path.insert(0, str(WEB / "graph"))

    out = DIST / "graph" / "index.html"
    out.parent.mkdir(parents=True, exist_ok=True)

    # The 3D page is the graph. It needs the vendored renderer, which is
    # gitignored, so the flat SVG stands in when that is absent rather than
    # leaving a dead link.
    lib = WEB / "vendor" / "3d-force-graph.min.js"
    if lib.is_file():
        import graph3d                                          # noqa: PLC0415
        out.write_text(graph3d.render(source, ROOT), encoding="utf-8")
        shutil.copy2(lib, out.parent / lib.name)
        print(f"build: 3D graph -> {out.relative_to(DIST)}")
    else:
        import graph as graph_mod                                # noqa: PLC0415
        out.write_text(graph_mod.render(source), encoding="utf-8")
        print("build: 3d-force-graph not vendored, using the flat graph "
              "(curl -sSL -o web/vendor/3d-force-graph.min.js "
              "https://unpkg.com/3d-force-graph@1/dist/3d-force-graph.min.js)")
    return out


# Guides live in docs/ rather than under web/, deliberately: they are read
# directly on GitHub and release/*/FLASH.md links into them by path. The build
# treats that directory as another source tree instead of moving them.
DOCS = ROOT / "docs"


def build_docs() -> list[Path]:
    """Render docs/*.md into dist/docs/ with a sidebar and an index."""
    if not DOCS.is_dir():
        print("build: no docs/ directory, skipping the guides")
        return []
    sys.path.insert(0, str(WEB / "site"))
    import markdown                                            # noqa: PLC0415

    pages = sorted(DOCS.glob("*.md"))
    if not pages:
        return []
    out_dir = DIST / "docs"
    out_dir.mkdir(parents=True, exist_ok=True)
    tpl = (WEB / "site" / "docs.tpl.html").read_text()

    def title_of(md: Path) -> str:
        for line in md.read_text().splitlines():
            if line.startswith("# "):
                return line[2:].strip()
        return md.stem.replace("-", " ")

    titles = {md: title_of(md) for md in pages}

    def tree_for(current: str) -> str:
        here = ' class="on"' if not current else ""
        rows = [f'<a href="index.html"{here}>All guides</a>']
        for md in pages:
            on = ' class="on"' if md.stem == current else ""
            rows.append(f'<a href="{md.stem}.html"{on}>{titles[md]}</a>')
        return "".join(rows)

    written = []
    for md in pages:
        body, _toc = markdown.render(md.read_text())
        page = (tpl.replace("@@TITLE@@", titles[md])
                   .replace("@@SLUG@@", md.stem)
                   .replace("@@TREE@@", tree_for(md.stem))
                   .replace("@@BODY@@", body))
        dest = out_dir / f"{md.stem}.html"
        dest.write_text(page, encoding="utf-8")
        written.append(dest)

    cards = "".join(
        f'<li><a href="{md.stem}.html"><span class="row-name">{titles[md]}</span>'
        f'<span class="row-desc">{md.stem}</span></a></li>' for md in pages)
    index = (tpl.replace("@@TITLE@@", "Guides")
                .replace("@@SLUG@@", "index")
                .replace("@@TREE@@", tree_for(""))
                .replace("@@BODY@@",
                         "<h1>Guides</h1><p>Bring-up, porting and bench notes "
                         "for the boards this firmware runs on.</p>"
                         '<ul class="rows mono">' + cards + "</ul>"))
    dest = out_dir / "index.html"
    dest.write_text(index, encoding="utf-8")
    written.append(dest)
    print(f"build: {len(pages)} guide(s) -> docs/")
    return written


def copy_trees() -> list[Path]:
    written: list[Path] = []
    for src, rel in TREES.items():
        if not src.is_dir():
            raise SystemExit(f"build: missing source tree {src}")
        for path in sorted(src.rglob("*")):
            if not path.is_file() or path.suffix in NOT_PUBLISHED:
                continue
            # Build inputs that live beside the pages they produce.
            if path.name.startswith("_ref-") or path.name.endswith(".tpl.html"):
                continue
            out = DIST / rel / path.relative_to(src)
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out)
            written.append(out)
    return written


LINK_RE = re.compile(r'(?:href|src)="([^"#][^"]*)"')


def check_links(pages: list[Path], have_twin: bool, have_graph: bool) -> list[str]:
    """Every relative href/src in the output must resolve to a real file.

    Two exceptions, each only when the optional tool behind it was absent:
    twin.js without emscripten, and the graph page without graphify. Both
    pages are worth publishing regardless, and neither tool is a build
    requirement, so a missing one must not fail the gate.
    """
    dead: list[str] = []
    for page in pages:
        if page.suffix != ".html":
            continue
        text = page.read_text(encoding="utf-8", errors="replace")
        for target in LINK_RE.findall(text):
            if target.startswith(("http://", "https://", "mailto:", "data:", "//",
                                  "about:", "blob:", "javascript:")):
                continue
            resolved = (page.parent / target.split("?")[0].split("#")[0]).resolve()
            if resolved.is_dir():
                resolved = resolved / "index.html"
            if not resolved.exists():
                if not have_twin and resolved.name == "twin.js":
                    continue
                if not have_graph and resolved.parent.name == "graph":
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
    written += build_docs()
    graph_page = build_graph()
    if graph_page:
        written.append(graph_page)
    print(f"build: {len(written)} file(s) -> {DIST.relative_to(ROOT)}")

    dead = check_links(written, have_twin, graph_page is not None)
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
