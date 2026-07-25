<!-- generated documentation — edit the source, not this file -->
# `tools/docs_twin.py`

Fold the interactive walk-up digital twin into the rendered site.

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

**discussed in** [`web-twin/README.md`](../../../web-twin/README.md)

## API

### `main() -> int`
`tools/docs_twin.py:61`

Copy the digital-twin firmware bundle into the site and inject a call-to-action card into the landing page's explore list.
