#!/usr/bin/env python3
"""Give the rendered site one curated reading order.

The generator ranks the guide list by keyword buckets, which is a reasonable
default and a poor journey: install and configure material was scattered, and
a reader finishing one page got no pointer to the next. This pass owns the
order in one place:

  * the landing page's Guides section is rebuilt into curated buckets
    (get it running first, then the two kinds of depth: research, and this
    project's own engineering log) — and because the sidebar shim mirrors the
    landing page's buckets, the sidebar follows automatically,
  * every page on the journey gets a prev/next pager, so there is always a
    next page and it is always the right one,
  * each guide's hero eyebrow names its bucket instead of the generic
    "Guide".

The buckets and the journey are the same list, so they cannot drift apart.
A guide added without a place in it fails the build here, on purpose: the
author decides where it belongs, or this pass would silently undo the point
of having a curated order.

Run from the repo root, after docs_start.py (start.html must exist to lead
the journey) and before docs_graph.py (whose sidebar shim reads the landing
page's buckets as rebuilt here).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SITE = Path("site")

# One entry per page, in reading order: (bucket, page slug). start.html and
# architecture.html ride the journey but are not landing-page guide rows —
# both already have their own primary placements.
JOURNEY = [
    ("Start here", "start"),
    ("Set up", "set-up"),
    ("Set up", "add-the-key"),
    ("Set up", "configuring"),
    ("Set up", "troubleshooting"),
    ("Set up", "home-assistant"),
    ("Hardware", "nrf5340-bringup"),
    ("Hardware", "nrf5340-wiring"),
    ("Hardware", "esp32-bringup"),
    ("Hardware", "hardware-validation"),
    # Research answers a question about the phone, the protocol or the radio —
    # something that stays true whoever builds the reader. The engineering log
    # below records what this repository itself built and what it cost. The
    # split matters for the reader arriving from a search engine: one half is
    # citable, the other is a changelog with prose.
    ("Research", "architecture"),
    ("Research", "protocol-research"),
    ("Research", "protocol-notes"),
    ("Research", "wireshark"),
    ("Research", "approach-direction"),
    ("Research", "passive-carry-verification"),
    # presence first, then what a signed distance is actually worth, then the
    # macOS consumer: each one assumes the previous.
    ("Research", "presence"),
    ("Research", "range-integrity"),
    ("Research", "uwb-mac-login"),
    ("Research", "porting-esp32-phase3"),
    ("Engineering log", "power-profile"),
    ("Engineering log", "chipset-memory"),
    ("Engineering log", "home-assistant-internals"),
    # the /twin spike belongs with the other Discord work, not with the radio
    ("Engineering log", "twin-worker-phase0"),
    # the user-facing page first, then the spike that proves it can exist
    ("Engineering log", "discord-activity"),
    ("Engineering log", "discord-activity-phase0"),
    ("Engineering log", "discord-activity-distribution"),
    ("Porting", "porting"),
    ("Porting", "porting-esp32"),
    ("Porting", "esp32-gotchas"),
    ("Porting", "dwm3001cdk-surgery"),
    ("Project", "reference"),
    ("Project", "RELEASING"),
    # Last in Project because these are the pages linked from outside the site:
    # Discord's developer portal wants a Privacy Policy URL and a Terms of
    # Service URL that both resolve.
    ("Project", "privacy"),
    ("Project", "terms"),
]
NOT_ROWS = {"start", "architecture"}

# Guides run to whatever comes next, which is the per-file listing on a freshly
# generated page and the close of the main column once docs_modules.py has moved
# that listing out. Both, because with no page generator configured the passes
# run over a site/ kept from the previous build, where it is already gone.
GUIDES_RE = re.compile(
    r'(<div class="section-h"><h2>Guides</h2><span class="rule"></span></div>\n)'
    r"(.*?)"
    r'(<div class="section-h" id="subsystems">|</main>)',
    re.S,
)
ROW_RE = re.compile(r'<li><a href="([^"]+?)\.html".*?</li>', re.S)
TITLE_RE = re.compile(r"<title>([^<]*)</title>")

PAGER_CSS = """<style id="gv-pager-css">
.gv-pager{display:flex;gap:.8rem;margin:2.6rem 0 .4rem;padding-top:1.4rem;border-top:1px solid var(--line)}
.gv-pager a{flex:1 1 0;min-width:0;max-width:calc(50% - .4rem);display:block;padding:.75rem .95rem;border:1px solid var(--line);border-radius:12px;background:var(--card);text-decoration:none;transition:border-color .16s ease,box-shadow .16s ease,transform .16s ease}
.gv-pager a:hover{border-color:var(--tint-line);box-shadow:var(--shadow);transform:translateY(-2px)}
/* The first and last page of a journey have only one neighbour. Without the
   cap above, that lone card stretched the full width with its text pinned to
   one edge and a hand's width of empty card beside it. */
.gv-pager .gv-next{text-align:right;margin-left:auto}
@media (max-width:640px){.gv-pager a{max-width:none}}
.gv-pager .gv-lab{display:block;font-size:.72rem;letter-spacing:.04em;text-transform:uppercase;color:var(--faint);margin-bottom:.28rem}
.gv-pager .gv-pt{display:block;font-weight:600;font-size:.92rem;color:var(--accent-ink);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.gv-pager .gv-ps{display:block;font-size:.76rem;color:var(--muted);margin-top:.18rem}
</style>"""


def fail(msg: str) -> int:
    """Print an error message to stderr prefixed with "docs_nav: " and return 1."""
    print(f"docs_nav: {msg}", file=sys.stderr)
    return 1


def page_title(slug: str) -> str:
    """Extract the page title from the <title> tag in an HTML file by path slug; return the slug itself if no title is found."""
    m = TITLE_RE.search((SITE / f"{slug}.html").read_text())
    return m.group(1) if m else slug


def curate_index(index: Path) -> int | None:
    """Rebuild the Guides section into the journey's buckets."""
    text = index.read_text()
    m = GUIDES_RE.search(text)
    if not m:
        return fail("index.html Guides section not found — generator layout changed?")

    rows = {slug: html for html, slug in
            ((r.group(0), r.group(1)) for r in ROW_RE.finditer(m.group(2)))}
    want = [slug for _, slug in JOURNEY if slug not in NOT_ROWS]
    missing = [s for s in want if s not in rows]
    extra = sorted(set(rows) - set(want))
    if missing or extra:
        return fail(
            f"guide rows and journey disagree — missing {missing or 'none'}, "
            f"unplaced {extra or 'none'}. Add new guides to JOURNEY."
        )

    out: list[str] = []
    open_bucket = None
    for bucket, slug in JOURNEY:
        if slug in NOT_ROWS:
            continue
        if bucket != open_bucket:
            if open_bucket is not None:
                out.append("</ul>")
            out.append(f'<div class="row-cap">{bucket}</div>\n<ul class="rows">')
            open_bucket = bucket
        out.append(rows[slug])
    out.append("</ul>\n")

    index.write_text(text[: m.end(1)] + "\n".join(out) + m.group(3) + text[m.end(3):])
    return None


def add_pagers() -> int | None:
    """Inject previous/next navigation cards before </main> on each journey page, updating the eyebrow label to the guide bucket name (except on the start page); do nothing if pager already present."""
    added = kept = 0
    for i, (bucket, slug) in enumerate(JOURNEY):
        page = SITE / f"{slug}.html"
        if not page.is_file():
            return fail(f"journey page {page} does not exist")
        text = page.read_text()

        if bucket != "Start here" and f'"eyebrow">Guide<' in text:
            text = text.replace('"eyebrow">Guide<', f'"eyebrow">{bucket}<', 1)

        if 'class="gv-pager"' in text:
            page.write_text(text)
            kept += 1
            continue

        cards = []
        if i > 0:
            psec, pslug = JOURNEY[i - 1]
            cards.append(
                f'<a class="gv-prev" href="{pslug}.html"><span class="gv-lab">'
                f'&#8592; Previous</span><span class="gv-pt">{page_title(pslug)}'
                f'</span><span class="gv-ps">{psec}</span></a>'
            )
        if i + 1 < len(JOURNEY):
            nsec, nslug = JOURNEY[i + 1]
            cards.append(
                f'<a class="gv-next" href="{nslug}.html"><span class="gv-lab">'
                f'Next up &#8594;</span><span class="gv-pt">{page_title(nslug)}'
                f'</span><span class="gv-ps">{nsec}</span></a>'
            )
        pager = PAGER_CSS + '\n<nav class="gv-pager" aria-label="Reading order">' \
            + "".join(cards) + "</nav>\n"

        end = text.rfind("</main>")
        if end < 0:
            return fail(f"{page} has no </main> to anchor the pager")
        page.write_text(text[:end] + pager + text[end:])
        added += 1
    note = f" ({kept} already wired)" if kept else ""
    print(f"    pager on {added} page(s){note}")
    return None


def main() -> int:
    """Check that the rendered site exists, rebuild the landing-page guides into journey buckets, inject previous/next pagers into journey pages, report results."""
    index = SITE / "index.html"
    if not index.is_file():
        print("    no rendered site — nothing to curate")
        return 0
    rc = curate_index(index)
    if rc is not None:
        return rc
    print("    landing-page guides rebuilt into journey buckets")
    rc = add_pagers()
    return 0 if rc is None else rc


if __name__ == "__main__":
    sys.exit(main())
