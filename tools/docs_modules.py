#!/usr/bin/env python3
"""Move the per-file reference listing off the landing page onto its own.

The generator ends index.html with every file in the tree — 331 of them at
this size, one row each, grouped by directory. That is a useful index and a
terrible last impression: it is roughly 60% of the landing page's bytes, it
buries the Guides section a reader actually wants, and nobody scrolls a
homepage looking for `ccc_shim_rx.c`.

This pass cuts that section out and republishes it as modules.html, so the
landing page ends at Guides and the listing gains a page where a directory
heading is a heading rather than a speed bump. The feature card that used to
jump to the #subsystems anchor now opens the page instead, and the listing
grows a per-directory jump strip, which the anchor version never had room
for.

Nothing here is curated: the rows, their order and their blurbs are the
generator's, moved verbatim. The page is assembled from an existing rendered
guide page, so it carries the current shell — sidebar, palette, theme toggle
and the other passes' injections.

Run from the repo root, after docs_nav.py (whose Guides regex still needs the
#subsystems heading in place) and before the link pass.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

SITE = Path("site")
TEMPLATE = SITE / "RELEASING.html"
INDEX = SITE / "index.html"
PAGE = SITE / "modules.html"

# The listing runs from its section heading to the close of the landing page's
# main column — it is the last thing on the page, which is the problem.
SECTION_RE = re.compile(
    r'<div class="section-h" id="subsystems">.*?</div>\n(.*?)(?=</main>)', re.S
)
CARD_RE = re.compile(
    r'(<li><a href=")#subsystems("[^>]*>.*?<span class="f-name">)[^<]*'
    r'(</span><span class="f-desc">)([^<]*)(</span>)',
    re.S,
)
DIR_RE = re.compile(r'<div class="ref-dir"><code>([^<]+)</code></div>')
NAV_RE = re.compile(r"^const NAV=(\{.*?\});$", re.M)

STYLE = """<style>
/* The hero's count chips. Same shapes docs_start.py gives them, restated here
   because that pass writes its styles into start.html only. */
.start-meta{display:flex;flex-wrap:wrap;gap:.5rem;margin-top:1.6rem}
.start-meta span{font-size:.76rem;color:var(--muted);border:1px solid var(--line);
  border-radius:99px;padding:.32rem .75rem;background:var(--card)}
.mod-cross{font-size:.9rem;color:var(--muted);margin:.4rem 0 0}
/* One jump strip over the whole listing: with a hundred directory groups the
   page is long enough that a reader who knows the directory should never have
   to scroll for it. Chips rather than a nested tree — the grouping is one
   level deep, so a tree would be an outline of nothing. */
.mod-jump{display:flex;flex-wrap:wrap;gap:.35rem;margin:1.2rem 0 2.2rem}
.mod-jump a{font-family:var(--mono);font-size:.74rem;color:var(--muted);
  text-decoration:none;padding:.24rem .6rem;border:1px solid var(--line);
  border-radius:99px;background:var(--card);
  transition:border-color .14s ease,color .14s ease,background-color .14s ease}
.mod-jump a:hover{border-color:var(--tint-line);color:var(--accent-ink);background:var(--tint)}
.doc .ref-dir{scroll-margin-top:5rem}
.doc .ref-dir code{font-size:.86rem}
</style>"""

LEDE = (
    "Every file in the tree, grouped by the directory it lives in, with the "
    "brief from its own docstring. Generated, not curated."
)

CROSS = (
    '<p class="mod-cross">Two neighbours answer different questions: '
    '<a href="architecture.html">Architecture</a> has the dependency graph and '
    "what imports what, and <a href=\"reference.html\">Reference</a> is the "
    "Doxygen tree, where the struct members and constant values live.</p>"
)


def slug(directory: str) -> str:
    """Turn a source directory path into an HTML id usable as a jump-strip anchor."""
    return "d-" + re.sub(r"[^a-z0-9]+", "-", directory.lower()).strip("-")


def anchored(listing: str) -> tuple[str, list[tuple[str, str]]]:
    """Give every directory heading in the listing an id, and return the listing with the directory names and ids collected in page order."""
    dirs: list[tuple[str, str]] = []

    def add(m: re.Match) -> str:
        """Rewrite one directory heading to carry an anchor id, recording it."""
        name = m.group(1)
        ident = slug(name)
        dirs.append((name, ident))
        return f'<div class="ref-dir" id="{ident}"><code>{name}</code></div>'

    return DIR_RE.sub(add, listing), dirs


def build_page(template: str, listing: str, dirs: list[tuple[str, str]]) -> str:
    """Render modules.html: the template shell with its hero and main column replaced by the moved listing, its jump strip and the cross-links to the two neighbouring reference trees."""
    jump = "".join(f'<a href="#{i}"><code>{n}</code></a>' for n, i in dirs)
    hero = (
        '<header class="hero-band"><div class="hero-in">'
        '<div class="eyebrow">Reference</div><h1>Modules</h1>'
        f'<p class="lede">{LEDE}</p>'
        f'<div class="start-meta"><span>{len(dirs)} directories</span>'
        f"<span>{listing.count('<li><a href=')} files</span></div>"
        "</div></header>"
    )
    body = (
        f'<main class="doc">\n{STYLE}\n{CROSS}\n'
        f'<nav class="mod-jump" aria-label="Directories">{jump}</nav>\n'
        f"{listing}\n</main>"
    )
    page = re.sub(r"<title>[^<]*</title>", "<title>Modules</title>", template, count=1)
    page = re.sub(
        r'(<meta property="og:title" content=")[^"]*(")', r"\1Modules\2", page, count=1
    )
    page = re.sub(r'data-active="[^"]*"', 'data-active="modules"', page, count=1)
    page = re.sub(
        r'<div class="crumb">.*?</div>',
        '<div class="crumb"><b>Modules</b></div>',
        page, count=1, flags=re.S,
    )
    # function replacements: the injected HTML lands verbatim rather than being
    # read as a replacement template, where a backslash escape would bite
    page = re.sub(
        r'<header class="hero-band">.*?</header>', lambda m: hero, page,
        count=1, flags=re.S,
    )
    return re.sub(
        r'<main class="doc[^"]*">.*?</main>', lambda m: body, page,
        count=1, flags=re.S,
    )


NAV_ANCHOR = '<script defer src="nav.js"></script>'

# Sitewide, after nav.js has built the tree: a Modules entry beside Architecture,
# which is where a reader looking for one file expects the other. Appended rather
# than inserted, so it sits after Architecture whatever else has been spliced in.
SIDEBAR = """<script id="gv-modules">
(function(){function go(){var dl=document.querySelector(".tree .doclinks");
if(!dl||dl.querySelector('a[href="modules.html"]'))return;
var a=document.createElement("a");a.href="modules.html";
a.className="doclink"+(location.pathname.split("/").pop()==="modules.html"?" on":"");
a.innerHTML='__ICON__';
a.appendChild(document.createTextNode("Modules"));dl.appendChild(a)}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go()})();
</script>""".replace(
    "__ICON__",
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" '
    'stroke-width="1.8" stroke-linejoin="round"><path d="M3 7.5 12 3l9 4.5v9L12 21l-9-4.5z"/>'
    '<path d="M3 7.5 12 12l9-4.5M12 12v9"/></svg>',
)


def wire_sidebar() -> int:
    """Inject the sidebar Modules entry into every rendered page that has not got it already. Returns the number of pages changed."""
    wired = 0
    for p in sorted(SITE.glob("*.html")):
        text = p.read_text()
        if 'id="gv-modules"' in text or NAV_ANCHOR not in text:
            continue
        p.write_text(text.replace(NAV_ANCHOR, NAV_ANCHOR + SIDEBAR, 1))
        wired += 1
    return wired


def add_search_row(title: str, page_name: str) -> None:
    """Add an entry for the given page to the nav.js search array if not already present, so the palette finds the page by name. Does nothing silently if nav.js is absent."""
    nav_path = SITE / "nav.js"
    if not nav_path.is_file():
        return
    text = nav_path.read_text()
    m = NAV_RE.search(text)
    if not m:
        return
    nav = json.loads(m.group(1))
    entry = ["page", title, "", page_name]
    if entry not in nav.get("search", []):
        nav["search"].insert(1, entry)
        nav_path.write_text(
            text[: m.start()] + "const NAV=" + json.dumps(nav) + ";" + text[m.end() :]
        )


def main() -> int:
    """Move the landing page's per-file reference listing into modules.html, repoint the feature card that used to jump to it, and register the new page with the search palette. Reports what moved; returns 1 if the landing page's layout no longer matches."""
    if not INDEX.is_file() or not TEMPLATE.is_file():
        print("    no rendered site — nothing to move")
        return 0

    index = INDEX.read_text()
    m = SECTION_RE.search(index)
    if not m:
        if PAGE.is_file() and 'href="modules.html"' in index:
            print(f"    reference listing already moved ({wire_sidebar()} newly wired)")
            return 0
        print(
            "docs_modules: landing page has no #subsystems listing to move — "
            "generator layout changed?",
            file=sys.stderr,
        )
        return 1

    listing, dirs = anchored(m.group(1).strip())
    if not dirs:
        print(
            "docs_modules: the listing carries no directory headings — "
            "generator layout changed?",
            file=sys.stderr,
        )
        return 1

    PAGE.write_text(build_page(TEMPLATE.read_text(), listing, dirs))
    index = index[: m.start()] + index[m.end() :]

    index, n = CARD_RE.subn(
        lambda c: c.group(1) + "modules.html" + c.group(2) + "Modules"
        + c.group(3) + c.group(4) + c.group(5),
        index,
        count=1,
    )
    INDEX.write_text(index)
    add_search_row("Modules", PAGE.name)
    wired = wire_sidebar()

    card = "feature card repointed" if n else "feature card anchor not found"
    print(f"    {len(dirs)} directories -> {PAGE}, {card}, sidebar on {wired} page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
