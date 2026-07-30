#!/usr/bin/env python3
"""Fold the interactive walk-up digital twin into the rendered site.

The page generator renders prose, navigation and reference pages; it does not
know about the standalone twin. This repo ships one:

  * web-twin/index.html — a self-contained page (inline JS/CSS, no network) that
    drives the reader's real unlock decision logic as a visitor walks a phone up
    to a door. It is themed off the same tokens as the site, so it drops in as
    site/twin.html and reads as the same product.

This pass copies that page in and adds one call-to-action on the landing page,
linking to it, anchored on the same explore-card list docs_media uses. The link
pass that runs later then validates site/twin.html resolves.

Idempotent on purpose: when the page generator is not configured, earlier passes
run over a site/ kept from a previous build, so the landing page may already
carry the CTA. Run from the repo root, after the generators and before the link
pass.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

SITE = Path("site")
TWIN_SRC = Path("web-twin/index.html")
TWIN_JS = Path("web-twin/twin.js")  # the page's firmware (woz_uwb compiled to WASM)
TWIN_DEST_NAME = "twin.html"

# The landing page's explore-card list; the CTA goes right before it, the same
# stable anchor docs_media.py hangs the demo figure on. Absent from a page the
# generator didn't emit this way — a layout change worth failing loudly on.
FEATS_ANCHOR = b'<ul class="feats">'
CTA_MARKER = b'class="twin-cta"'

# Self-contained injection (its own <style>), styled through the site tokens so
# it follows the theme — the same approach docs_media.py takes for its figure.
# The prose column needs min-width:0 or the nowrap "Open the twin" tail wins
# the flex fight and squeezes it to one word per line; below 640px the tail
# drops to its own row instead of competing at all. The icon is an inline SVG
# rather than an emoji, so it inherits the card's colour and renders the same
# on every platform.
DOOR = (
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" '
    'stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" '
    'aria-hidden="true"><path d="M4 21h16"/><path d="M7 21V4a1 1 0 0 1 '
    '1-1h8a1 1 0 0 1 1 1v17"/><circle cx="14" cy="12.5" r="1" '
    'fill="currentColor" stroke="none"/></svg>'
)

CTA = f"""<style>
.twin-cta{{display:flex;align-items:center;gap:1rem;margin:2rem 0 .4rem;padding:1rem 1.2rem;
  text-decoration:none;border:1px solid var(--tint-line);border-radius:14px;background:var(--tint);
  color:var(--ink);transition:border-color .15s,transform .15s,box-shadow .15s}}
.twin-cta:hover{{border-color:var(--accent);transform:translateY(-1px);box-shadow:var(--shadow)}}
.twin-cta .tc-ic{{flex:none;width:2.4rem;height:2.4rem;display:grid;place-items:center;border-radius:11px;
  background:var(--accent);color:#fff}}
.twin-cta .tc-ic svg{{width:1.25rem;height:1.25rem}}
.twin-cta .tc-t{{flex:1;min-width:0}}
.twin-cta .tc-t b{{display:block;color:var(--strong);font-size:1rem}}
.twin-cta .tc-t span{{color:var(--muted);font-size:.85rem}}
.twin-cta .tc-go{{margin-left:auto;color:var(--accent-ink);font-weight:600;font-size:.85rem;white-space:nowrap}}
@media (max-width:640px){{.twin-cta{{flex-wrap:wrap}}
.twin-cta .tc-t,.twin-cta .tc-go{{flex-basis:100%;margin-left:0}}}}
</style>
<a class="twin-cta" href="{TWIN_DEST_NAME}">
<span class="tc-ic">{DOOR}</span>
<span class="tc-t"><b>Interactive digital twin</b><span>Walk a phone up to the door and watch the reader's real
unlock logic react &mdash; BLE, UWB ranging, the trust gate, the bolt.</span></span>
<span class="tc-go">Open the twin &rarr;</span>
</a>
""".encode()


def main() -> int:
    """Copy the digital-twin firmware bundle into the site and inject a call-to-action card into the landing page's explore list."""
    index = SITE / "index.html"
    if not index.is_file():
        print("    no rendered site — nothing to fold the twin into")
        return 0

    if not TWIN_SRC.is_file():
        print(f"docs_twin: missing {TWIN_SRC}", file=sys.stderr)
        return 1
    if not TWIN_JS.is_file():
        print(f"docs_twin: missing {TWIN_JS} — run `make twin-wasm`", file=sys.stderr)
        return 1

    # The page + its WASM firmware are a flat file pair; a plain copy renders
    # identically to file:// (twin.html loads twin.js relatively).
    shutil.copyfile(TWIN_SRC, SITE / TWIN_DEST_NAME)
    shutil.copyfile(TWIN_JS, SITE / TWIN_JS.name)

    raw = index.read_bytes()
    if CTA_MARKER in raw:
        print("    twin CTA already present")
    elif FEATS_ANCHOR not in raw:
        print(
            "docs_twin: landing page has no explore-card list to anchor the "
            "twin CTA on — generator layout changed?",
            file=sys.stderr,
        )
        return 1
    else:
        index.write_bytes(raw.replace(FEATS_ANCHOR, CTA + FEATS_ANCHOR, 1))
        print(f"    twin copied to site/{TWIN_DEST_NAME} + CTA injected into index.html")

    return 0


if __name__ == "__main__":
    sys.exit(main())
