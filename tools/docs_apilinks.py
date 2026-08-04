#!/usr/bin/env python3
"""Point each narrative page at the declarations it is describing.

The guides and the Doxygen tree are written for the same reader at two
different moments and never referred to each other: a page explains what the
STS ladder defends against and stops there, while `ccc_sts.h` states exactly
what the ladder is, three clicks away with no path between them.

This pass adds one block at the foot of the pages that have a counterpart —
the header the prose is about, with a line saying why it is the one to open.
The mapping is curated here rather than derived, because "the API this page
is about" is a judgement; what is not curated is whether the target exists.
Every header named below is resolved against the rendered tree and a miss
fails the build, so a renamed or newly-undocumented header cannot rot into a
dead link.

Doxygen omits undocumented symbols from the reference tree but still renders
a source view, so a header with no doc comments resolves to its source page
and the row says so.

Run from the repo root, after doxygen and after docs_nav.py (the block goes
above the pager, not below it), and before the link pass.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SITE = Path("site")
API = SITE / "api"

# page slug -> (header file name, why this reader wants it). Order is the
# order they appear on the page, most central first.
XREF: dict[str, tuple[tuple[str, str], ...]] = {
    "protocol-research": (
        ("aliro_reader.h", "The reader session and transaction layer this "
                           "report is describing from the outside."),
        ("aliro_uwb_msg_spec.h", "The UWB ranging-service framing constants: "
                                 "the frame layouts named throughout."),
        ("ccc_session.h", "The seam that turns an authenticated session's URSK "
                          "and M1-M4 setup into ranging parameters."),
    ),
    "protocol-notes": (
        ("access_document.h", "The parsed access document, including the "
                              "validity period these notes are about."),
    ),
    "wireshark": (
        ("aliro_advtag.h", "The Dynamic Tag derivation the dissector resolves "
                           "in the 0xFFF2 advertisement."),
        ("aliro_uwb_msg_spec.h", "Framing constants, for the parts a passive "
                                 "sniffer does not get to see."),
    ),
    "approach-direction": (
        ("aliro_approach.h", "Thresholds, dwell times and the predictive "
                             "unlock flag the control is steering."),
    ),
    "power-profile": (
        ("aliro_rssi_gate.h", "The gate itself: EWMA smoothing, hysteresis, "
                              "and the rise-rate fast open."),
    ),
    "range-integrity": (
        ("ccc_sts.h", "The scrambled timestamp sequence whose quality layer 2 "
                      "is checking."),
        ("fira_session.h", "Where the four layers are defined."),
        ("flight_recorder.h", "What the board keeps so a range can be argued "
                              "about afterwards."),
    ),
    "porting-esp32-phase3": (
        ("aliro_kdf.h", "The URSK the whole phase is working towards."),
        ("ccc_kdf.h", "The CCC key schedule it feeds."),
        ("ccc_shim.h", "Where the derived key is bound to the radio."),
    ),
}

STYLE_ID = "gv-apixref"
STYLE = """<style id="gv-apixref-css">
.api-xref{margin:2.4rem 0 .4rem;padding:1rem 1.15rem;border:1px solid var(--line);
  border-radius:14px;background:var(--card)}
.api-xref .ax-h{font-size:.72rem;font-weight:650;letter-spacing:.11em;
  text-transform:uppercase;color:var(--faint);margin-bottom:.15rem}
.api-xref .rows{margin:.2rem 0 0}
.api-xref .rows a{border-radius:9px;padding-left:.55rem;padding-right:.55rem;
  transition:background-color .14s ease}
.api-xref .rows a:hover{background:var(--tint)}
.api-xref .row-name code{background:none;border:0;padding:0;font-size:.88rem}
.api-xref .ax-src{font-family:var(--mono);font-size:.68rem;color:var(--faint);
  margin-left:.45rem;letter-spacing:.04em}
</style>"""

PAGER_RE = re.compile(r'<style id="gv-pager-css">|<nav class="gv-pager"')


def doxygen_page(header: str) -> tuple[str, bool] | None:
    """Resolve a header file name to its page in the rendered Doxygen tree, preferring the documented page over the source view. Returns (path relative to site/, documented) or None when the tree has neither."""
    stem = header.replace("_", "__").replace(".", "_8")
    for name, documented in ((f"{stem}.html", True), (f"{stem}_source.html", False)):
        if (API / name).is_file():
            return f"api/{name}", documented
    return None


def block(entries: tuple[tuple[str, str], ...]) -> str | None:
    """Render the cross-reference block for one page, or None if any header named for it is absent from the rendered tree."""
    rows = []
    for header, why in entries:
        hit = doxygen_page(header)
        if hit is None:
            print(f"docs_apilinks: no reference page for {header}", file=sys.stderr)
            return None
        href, documented = hit
        tag = "" if documented else '<span class="ax-src">source</span>'
        rows.append(
            f'<li><a href="{href}"><span class="row-name"><code>{header}</code>'
            f'</span>{tag}<span class="row-desc">{why}</span></a></li>'
        )
    return (
        f'{STYLE}\n<aside class="api-xref">'
        f'<div class="ax-h">In the API reference</div>'
        f'<ul class="rows mono">{"".join(rows)}</ul></aside>\n'
    )


def main() -> int:
    """Add an API cross-reference block to every narrative page that has one, above the reading-order pager. Reports what was linked; returns 1 when a page or a named header is missing."""
    # site/ exists as soon as doxygen has run, so it says nothing about whether
    # the guides were rendered. With no page generator configured they are not,
    # and a missing narrative page is then the expected state rather than the
    # renamed-guide this pass is here to catch.
    if not (SITE / "index.html").is_file():
        print("    no rendered site — nothing to cross-link")
        return 0
    if not API.is_dir():
        print("    no reference tree — cross-links skipped")
        return 0

    linked = kept = 0
    for slug, entries in XREF.items():
        page = SITE / f"{slug}.html"
        if not page.is_file():
            print(f"docs_apilinks: {page} does not exist", file=sys.stderr)
            return 1
        text = page.read_text()
        if STYLE_ID in text:
            kept += 1
            continue
        html = block(entries)
        if html is None:
            return 1
        # above the pager when one is there, so the page still ends on its
        # next-page card rather than on a reference list
        m = PAGER_RE.search(text)
        cut = m.start() if m else text.rfind("</main>")
        if cut < 0:
            print(f"docs_apilinks: {page} has no </main> to anchor to", file=sys.stderr)
            return 1
        page.write_text(text[:cut] + html + text[cut:])
        linked += 1

    note = f" ({kept} already linked)" if kept else ""
    print(f"    API cross-links on {linked} page(s){note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
