#!/usr/bin/env python3
"""Fill the reference pages the page generator leaves bare.

The generator documents code where it is defined: functions with bodies,
structs, inline helpers. A header that only *declares* things — prototypes,
macros, enums — renders as a hero line and a "used by" row, which reads as
an empty page even when every declaration in the file carries a doc comment.

This pass parses those headers straight from the working tree and appends
the missing declarations in the generator's own api-entry markup, so the
"On this page" rail and the search palette treat them like any other entry:

  * function prototypes (with their /** brief */ if present),
  * documented #defines, plus undocumented value-carrying ones — a pin map
    is worth listing even uncommented; include guards are not,
  * enum/struct/union declarations the page does not already show.

Anything the page already renders is skipped by anchor id, so running after
the generator adds only what it left out. New entries are also appended to
the search index in nav.js. Run from the repo root, after docs_graph.py and
before the link pass.
"""

from __future__ import annotations

import html
import json
import re
import sys
from pathlib import Path

SITE = Path("site")

TITLE_RE = re.compile(r"<title>((?:modules|ports)/[\w./-]+\.h)</title>")
DOC_OPEN_RE = re.compile(r"^/\*\*")
DEFINE_RE = re.compile(r"#\s*define\s+([A-Za-z_]\w*)(\([^)]*\))?\s*(.*)$")
TAG_RE = re.compile(r"typedef\s+(?:enum|struct|union)|^(?:enum|struct|union)\s")
TAG_NAME_RE = re.compile(r"(?:enum|struct|union)\s+([A-Za-z_]\w*)")
FNPTR_RE = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")
PROTO_RE = re.compile(
    r"(.+?)\b([A-Za-z_]\w*)\s*\((.*)\)\s*(?:[A-Z_][A-Z0-9_]*(?:\([^)]*\))?\s*)?$"
)
NAV_RE = re.compile(r"^const NAV=(\{.*?\});$", re.M)

KINDS = {
    "function": "F",
    "macro": "#",
    "class": "C",
}


def clean_brief(raw: str) -> str:
    """Doc-comment text -> one inline-HTML sentence, generator style."""
    t = re.sub(r"^/\*\*", "", raw)
    t = re.sub(r"\*/\s*$", "", t)
    t = " ".join(re.sub(r"^\s*\*\s?", "", ln) for ln in t.splitlines())
    t = re.sub(r"^\s*@brief\s+", "", t.strip())
    t = html.escape(" ".join(t.split()), quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    return re.sub(r"@p\s+(\w+)", r"<code>\1</code>", t)


def classify(decl: str) -> tuple[str, str, str] | None:
    """A flattened `...;` declaration -> (kind, name, signature) or None."""
    d = decl.rstrip(";").strip()
    if d.startswith("typedef"):
        m = FNPTR_RE.search(d) or re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?$", d)
        return ("class", m.group(1), d) if m else None
    m = PROTO_RE.match(d)
    if m and "(" not in m.group(1) and "=" not in m.group(1):
        ret = " ".join(m.group(1).split()).strip()
        if not ret or ret.split()[0] in ("return", "goto", "else"):
            return None
        args = " ".join(m.group(3).split())
        return "function", m.group(2), f"{ret} {m.group(2)}({args})"
    return None


def parse_header(text: str) -> list[tuple[int, str, str, str, str]]:
    """-> [(line, kind, name, signature, brief-html)] for every declaration."""
    out = []
    lines = text.splitlines()
    i, depth, brief = 0, 0, ""
    while i < len(lines):
        s = lines[i].strip()
        if depth > 0:
            depth = max(0, depth + s.count("{") - s.count("}"))
            i += 1
            continue
        if DOC_OPEN_RE.match(s):
            start = i
            while "*/" not in lines[i] and i + 1 < len(lines):
                i += 1
            raw = "\n".join(lines[start : i + 1])
            brief = "" if "@file" in raw.split("\n")[0] else clean_brief(raw)
            i += 1
            continue
        if s.startswith("/*"):
            while "*/" not in lines[i] and i + 1 < len(lines):
                i += 1
            brief, i = "", i + 1
            continue
        if not s or s.startswith("//") or s in ("}",) or s.startswith('extern "C"'):
            brief, i = "", i + 1
            continue
        if s.startswith("#"):
            m = DEFINE_RE.match(s)
            if m:
                name, value = m.group(1), m.group(3).split("/*")[0].strip()
                guard = name.endswith(("_H", "_H_")) or not value.rstrip("\\").strip()
                if not guard:
                    sig = re.sub(r"/\*.*$", "", s).rstrip(" \\").strip()
                    out.append((i + 1, "macro", name, sig, brief))
            brief = ""
            while lines[i].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
            i += 1
            continue
        start, decl = i, s
        while not re.search(r"[;{]\s*(//.*|/\*.*)?$", decl) and i + 1 < len(lines):
            i += 1
            decl += " " + lines[i].strip()
        decl = re.sub(r"/\*.*?\*/", " ", decl)
        decl = re.sub(r"//.*$", "", decl).strip()
        if "{" in decl:
            m = TAG_NAME_RE.search(decl) if TAG_RE.search(decl) else None
            if m:
                tag = decl[: decl.index("{")].strip()
                out.append((start + 1, "class", m.group(1), tag, brief))
            depth = decl.count("{") - decl.count("}")
            brief, i = "", i + 1
            continue
        if decl.endswith(";"):
            got = classify(decl)
            if got:
                out.append((start + 1, *got, brief))
        brief, i = "", i + 1
    return out


def entry_html(path: str, line: int, kind: str, name: str, sig: str, brief: str) -> str:
    """Render one API entry as HTML: function or macro signature with kind badge, source location, and optional docstring paragraph. Highlights the symbol name in bold."""
    code = html.escape(sig, quote=False).replace(name, f"<b>{name}</b>", 1)
    body = f"\n<p>{brief}</p>" if brief else ""
    return (
        f'<section class="api-entry" id="{name}">\n'
        f'<h3><span class="kindb" title="{kind}">{KINDS[kind]}</span>'
        f"<code>{code}</code></h3>\n"
        f'<div class="src">{path}:{line}</div>{body}\n'
        f"</section>\n"
    )


API_OPEN = (
    '<div class="section-h"><h2>API</h2><span class="rule"></span></div>\n'
    '<div class="api">\n'
)


def fill_page(page_path: Path) -> list[tuple[str, str, str, str]]:
    """-> [(kind, name, anchor-href)] appended to this page."""
    page = page_path.read_text()
    m = TITLE_RE.search(page)
    if not m:
        return []
    src = Path(m.group(1))
    if not src.is_file():
        return []
    fresh = [
        e for e in parse_header(src.read_text()) if f'id="{e[2]}"' not in page
    ]
    if not fresh:
        return []
    blocks = "".join(entry_html(str(src), ln, k, n, s, b) for ln, k, n, s, b in fresh)
    if '<div class="api">' in page:
        cut = page.rfind("</div>\n</main>")
        if cut < 0:
            return []
        page = page[:cut] + blocks + page[cut:]
    elif "</main>" in page:
        page = page.replace("</main>", API_OPEN + blocks + "</div>\n</main>", 1)
    else:
        return []
    page_path.write_text(page)
    return [(k, n, src.name, f"{page_path.name}#{n}") for _, k, n, _, _ in fresh]


def index_rows(rows: list[tuple[str, str, str, str]]) -> int:
    """Append new entries to the search palette's index in nav.js."""
    nav_path = SITE / "nav.js"
    if not nav_path.is_file():
        return 0
    text = nav_path.read_text()
    m = NAV_RE.search(text)
    if not m:
        print("docs_api: nav.js NAV constant not found — generator changed?",
              file=sys.stderr)
        return -1
    nav = json.loads(m.group(1))
    have = {(r[1], r[3]) for r in nav.get("search", [])}
    added = 0
    for kind, name, ctx, href in rows:
        if (name, href) in have:
            continue
        nav["search"].append([kind, name, ctx, href])
        added += 1
    if added:
        out = text[: m.start()] + "const NAV=" + json.dumps(nav) + ";" + text[m.end() :]
        nav_path.write_text(out)
    return added


def main() -> int:
    """Fill all rendered site pages with API declarations: parse symbol index, extract signatures and briefs, inject entries into pages, and merge new rows into the search index. Returns count of declarations added to the index, or -1 on error."""
    pages = sorted(SITE.glob("*.html"))
    if not pages:
        print("    no rendered site — nothing to fill")
        return 0
    rows: list[tuple[str, str, str, str]] = []
    filled = 0
    for p in pages:
        got = fill_page(p)
        if got:
            filled += 1
            rows += got
    added = index_rows(rows)
    if added < 0:
        return 1
    print(f"    {len(rows)} declaration(s) filled in on {filled} page(s), "
          f"{added} added to the search index")
    return 0


if __name__ == "__main__":
    sys.exit(main())
