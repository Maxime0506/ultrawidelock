<!-- generated documentation — edit the source, not this file -->
# `tools/docs_theme.py`

Retheme the rendered site: warm paper surfaces, serif display headings.

The page generator ships a neutral blue-on-gray look. This pass restyles the
rendered output — never the generator — into the warm editorial style the
project wants: ivory paper backgrounds, near-black ink, a terracotta accent,
tan links in dark mode, and a serif display face over the headings. Two files
carry the whole theme:

  * site/style.css — every generated page links it, and every earlier pass
    styles its injections through the sheet's custom properties (--ground,
    --ink, --accent, …). Appending a redefinition of those properties at the
    end of the sheet wins the cascade everywhere at once, so the sidebar, the
    landing cards, the command chips and the search palette all follow without
    touching a single HTML file. A component layer after the variables covers
    what variables cannot express: heading typefaces, the always-dark code
    panels, hover and focus behaviour, and the two layout repairs below.
  * site/api/doxygen-awesome.css — the reference tree's stylesheet exposes the
    same kind of seam (--page-background-color, --primary-color, …), so the
    API pages get the matching palette and headline face.

Three faces, all from Google Fonts, pulled with one @import — which CSS
requires ahead of every rule, so the import is prepended while the overrides
are appended. Source Serif 4 sets the display headings, Inter the body and UI
(the system stack stays as the fallback), JetBrains Mono the code. Every face
is loaded with display=swap, so text paints in the fallback immediately.

The palette is generated from PALETTE below rather than written out four
times: the base sheet scopes its own variables as OS preference first,
explicit toggle second, and emit() mirrors that exactly so the toggle keeps
winning. Every foreground token in PALETTE clears 4.5:1 against the surfaces
it is actually painted on — see the ratios recorded beside each one.

Two layout repairs ride along, because both are cascade-only:

  * the hero band centred its own text on the full content width while the
    article below it is pushed left by the "On this page" rail, so a page
    title and its first heading sat ~88px apart. The hero adopts the article
    grid when the rail is present, and the two line up.
  * the landing hero widens to 74rem and had no gutter at all above 1280px,
    so the terminal card ran flush into the window edge.

Idempotent like the other passes: a marker comment guards both files, so
re-running over a kept site/ changes nothing. Run from the repo root, any time
after the generators; it edits only the two stylesheets, no page markup.

**discussed in** [`web-twin/README.md`](../../../web-twin/README.md)

## API

### `emit(scheme: str) -> str`
`tools/docs_theme.py:151`

Return PALETTE[scheme] as a semicolon-joined run of custom properties.

### `theme(sheet: Path, css: str) -> str`
`tools/docs_theme.py:409`

Prepend the font import and append the overrides; report what happened.

**called by** `main`

### `main() -> int`
`tools/docs_theme.py:418`

Apply CSS theme overrides (fonts and color scheme) to the main site shell and API reference tree if they exist; report what was themed.

**calls** `theme`
