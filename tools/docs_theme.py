#!/usr/bin/env python3
"""Retheme the rendered site: warm paper surfaces, serif display headings.

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
"""

from __future__ import annotations

import sys
from pathlib import Path

SITE = Path("site")
SHEET = SITE / "style.css"
API_SHEET = SITE / "api" / "doxygen-awesome.css"

MARK = "/* aliro-theme */"

FONT_IMPORT = (
    "@import url('https://fonts.googleapis.com/css2"
    "?family=Inter:wght@300..700"
    "&family=JetBrains+Mono:wght@400..600"
    "&family=Source+Serif+4:ital,opsz,wght@0,8..60,400..600;1,8..60,400..600"
    "&display=swap');\n"
)

# One palette, two schemes. The contrast ratio beside each foreground is
# against the surfaces it actually lands on (ground / raise / card), computed
# with the WCAG relative-luminance formula; nothing here is below 4.5:1, which
# is the small-text bar. The previous palette had --faint at 2.68:1 in light
# mode, under even the 3:1 large-text bar, and the accent at 3.70:1 while the
# uppercase eyebrows are painted in it at 12px.
PALETTE = {
    "light": {
        "ground": "#faf9f5",
        "surface": "#faf9f5",
        "raise": "#f0eee6",
        "card": "#ffffff",
        "ink": "#3d3d3a",  # 10.34 / 9.38 / 10.90
        "strong": "#141413",  # 17.50
        "muted": "#5c5b54",  # 6.47 / 5.87 / 6.82
        "faint": "#6b6960",  # 5.23 / 4.74 / 5.51
        "line": "rgba(31,30,29,.14)",
        "hairline": "rgba(31,30,29,.07)",
        "accent": "#c96442",  # fills, rules and icons only — never small text
        "accent-ink": "#a94c2d",  # accent as text: 5.30 / 4.81 / 5.59
        "btn": "#b35232",  # white label on it: 5.04
        "btn-hover": "#9c4221",  # 6.53
        "btn-ink": "#ffffff",
        "ok": "#297347",  # 5.21 / 4.72
        "tint": "rgba(201,100,66,.10)",
        "tint-line": "rgba(201,100,66,.30)",
        "shadow": (
            "0 1px 2px rgba(31,30,29,.04),0 12px 32px -20px rgba(31,30,29,.16)"
        ),
        "shadow-lg": "0 30px 70px -26px rgba(31,30,29,.35)",
        "shadow-hover": (
            "0 2px 4px rgba(31,30,29,.05),0 18px 40px -22px rgba(31,30,29,.28)"
        ),
        "herotint": "rgba(201,100,66,.06)",
        "scrollthumb": "rgba(31,30,29,.18)",
    },
    "dark": {
        "ground": "#1f1e1d",
        "surface": "#1b1a19",
        "raise": "#302f2c",
        "card": "#262624",
        "ink": "#c2c0b6",  # 9.12 / 7.34 / 8.31
        "strong": "#faf9f5",  # 15.80
        "muted": "#a5a39a",  # 6.58 / 5.29 / 6.00
        "faint": "#9b998f",  # 5.82 / 4.68 / 5.24
        "line": "rgba(250,249,245,.13)",
        "hairline": "rgba(250,249,245,.07)",
        "accent": "#d97757",  # 5.33
        "accent-ink": "#d4a27f",  # 7.35
        "btn": "#d97757",
        "btn-hover": "#e08c6d",
        "btn-ink": "#1f1e1d",  # on --btn: 5.33
        "ok": "#8ac9a1",  # 8.68 / 6.98
        "tint": "rgba(212,162,127,.13)",
        "tint-line": "rgba(212,162,127,.36)",
        "shadow": "0 1px 2px rgba(0,0,0,.5),0 14px 34px -16px rgba(0,0,0,.55)",
        "shadow-lg": "0 34px 80px -24px rgba(0,0,0,.7)",
        "shadow-hover": "0 2px 4px rgba(0,0,0,.5),0 20px 44px -18px rgba(0,0,0,.65)",
        "herotint": "rgba(212,162,127,.05)",
        "scrollthumb": "rgba(250,249,245,.18)",
    },
}

# Scheme-independent: the code panels stay dark on paper in both schemes, so
# their colours are absolute rather than per-scheme.
CONSTANTS = {
    "serif": '"Source Serif 4",Georgia,"Times New Roman",serif',
    "sans": (
        '"Inter",-apple-system,BlinkMacSystemFont,"SF Pro Text","Segoe UI",'
        "Roboto,Helvetica,Arial,sans-serif"
    ),
    "mono": (
        '"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Menlo,'
        "Consolas,monospace"
    ),
    "codebg": "#1f1e1d",
    "codeink": "#dedcd3",  # 12.11 on --codebg
    "codeline": "rgba(250,249,245,.09)",
    "codehead": "#171614",
    "codeprompt": "#e08c6d",  # 6.45 on --codebg
    "codedim": "#9a968a",  # 5.63 on --codebg
}


def emit(scheme: str) -> str:
    """Return PALETTE[scheme] as a semicolon-joined run of custom properties."""
    return "".join(f"--{k}:{v};" for k, v in PALETTE[scheme].items())


LIGHT, DARK = emit("light"), emit("dark")
CONST = "".join(f"--{k}:{v};" for k, v in CONSTANTS.items())

THEME = f"""
{MARK}
/* ---- tokens ----------------------------------------------------------- */
/* Scoped exactly as the base sheet scopes its own: OS preference first, the
   explicit toggle second, so the toggle keeps winning either way. */
:root{{{CONST}{LIGHT}}}
@media (prefers-color-scheme:dark){{:root{{{DARK}}}}}
:root[data-theme="light"]{{{LIGHT}}}
:root[data-theme="dark"]{{{DARK}}}

/* The base sheet pins color-scheme to dark and only unpins it for the
   explicit light toggle, so a first visit on a light desktop got this
   theme's ivory page with dark-rendered scrollbars, form fields and
   spinners. Follow the palette instead. `:root` outranks its `html`. */
:root{{color-scheme:light}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{color-scheme:dark}}}}
:root[data-theme="light"]{{color-scheme:light}}
:root[data-theme="dark"]{{color-scheme:dark}}

/* ---- type ------------------------------------------------------------- */
/* A larger, airier measure throughout. Bumping the root size scales every
   rem-based component with it, sidebar included. */
html{{font-size:17px}}
:root{{--article:48rem}}
body{{font-family:var(--sans);letter-spacing:-.011em;font-synthesis-weight:none}}
code,kbd,pre,.mono{{font-family:var(--mono)}}

/* Display headings. Direct children only: the nested h2s — the uppercase
   .section-h rails and the per-module .arch-sec headings — keep the sans
   they were designed around. The .mono variants outrank these on
   specificity, so API titles stay monospace untouched. */
.doc>h1,.doc>h2,.doc>h3,.hero-in h1,.hero h1{{font-family:var(--serif);
  letter-spacing:0;font-optical-sizing:auto;text-wrap:balance}}
.doc>h1{{font-size:2.35rem;font-weight:430;line-height:1.2}}
.hero-in h1{{font-size:2.45rem;font-weight:430;line-height:1.2}}
.hero h1{{font-size:clamp(2.6rem,5vw,3.4rem);font-weight:420;line-height:1.08}}
.doc>h2{{font-size:1.6rem;font-weight:500;margin:3rem 0 1rem}}
.doc>h3{{font-size:1.22rem;font-weight:550;margin:2rem 0 .6rem}}
.doc{{line-height:1.78}}
.doc p{{margin:1.05rem 0;text-wrap:pretty}}
.lede{{font-size:1.16rem;line-height:1.65;color:var(--muted)}}
.eyebrow{{color:var(--accent-ink);letter-spacing:.13em}}

/* Links carry the accent and a light underline that firms up on hover —
   the affordance the near-black link colour used to swallow. */
a{{color:var(--accent-ink)}}
.doc p a,.doc li a,.doc td a{{text-decoration-thickness:1px;
  text-decoration-color:color-mix(in srgb,var(--accent-ink) 38%,transparent)}}
.doc p a:hover,.doc li a:hover,.doc td a:hover{{text-decoration-color:var(--accent-ink)}}
.row-name{{color:var(--accent-ink)}}

/* ---- code ------------------------------------------------------------- */
/* Panels stay dark on paper in both schemes; inline code keeps the light
   chip the base sheet gives it. */
pre{{background:var(--codebg);border-color:var(--codeline);color:var(--codeink);
  padding:1rem 1.15rem;font-size:.86rem;line-height:1.7}}
p code,li code,.chips code,.xref code,dd code{{padding:.12em .38em;
  border-radius:6px;font-size:.85em}}
.copy{{background:color-mix(in srgb,var(--codebg) 82%,transparent);
  border-color:var(--codeline);color:var(--codedim);backdrop-filter:blur(6px)}}
.copy:hover{{color:var(--codeink);border-color:var(--codeprompt)}}
.copy.done{{color:var(--ok);border-color:var(--ok)}}

/* The hero terminal and the copyable command chips shipped a green prompt
   and green-grey chrome from the generator's own palette. Retint both. */
.term{{background:var(--codebg);border-color:var(--codeline)}}
.t-head{{background:var(--codehead);border-bottom-color:var(--codeline)}}
.t-title{{color:var(--codedim)}}
.t-body{{color:var(--codeink)}}
.t-p{{color:var(--codeprompt)}}
.t-c{{color:var(--codedim)}}
.t-cur{{background:var(--codeink)}}
.cmdchip{{background:var(--card);border-color:var(--line);border-radius:12px;
  transition:border-color .16s ease,box-shadow .16s ease}}
.cmdchip:hover{{border-color:var(--tint-line);box-shadow:var(--shadow)}}
.cmdchip .t-p{{color:var(--accent-ink)}}
.cmdchip button{{background:var(--raise);border-color:var(--line)}}
.cmdchip button:hover{{color:var(--accent-ink);border-color:var(--tint-line)}}

/* API signatures are code blocks, so they read as code blocks — the light
   panel made them the one kind of code on the site that was not. */
.api-entry h3 code{{background:var(--codebg);border-color:var(--codeline);
  color:var(--codeink)}}
.api-entry h3 code b{{color:var(--codeprompt)}}
.kindb{{background:var(--tint);color:var(--accent-ink)}}
.src{{color:var(--faint)}}

/* ---- surfaces --------------------------------------------------------- */
.hero-band{{background:linear-gradient(180deg,var(--herotint),transparent 82%)}}
.btn{{transition:border-color .16s ease,background .16s ease,color .16s ease,
  box-shadow .16s ease}}
.btn:hover{{border-color:var(--tint-line);color:var(--accent-ink);
  box-shadow:var(--shadow)}}
.btn-primary,:root[data-theme="light"] .btn-primary{{background:var(--btn);
  border-color:var(--btn);color:var(--btn-ink)}}
.btn-primary:hover,:root[data-theme="light"] .btn-primary:hover{{
  background:var(--btn-hover);border-color:var(--btn-hover);
  color:var(--btn-ink);box-shadow:var(--shadow)}}

/* Cards lift a little on hover — 160ms, transform and shadow only, so
   nothing reflows. The reduced-motion block in the base sheet kills it. */
.feats a,.path,.rows a,.twin-cta{{transition:border-color .16s ease,
  box-shadow .16s ease,transform .16s ease}}
.feats a{{border-radius:16px}}
.feats a:hover{{border-color:var(--tint-line);box-shadow:var(--shadow-hover);
  transform:translateY(-2px)}}
.f-ico,.p-ico{{background:var(--tint);color:var(--accent-ink)}}
.pill{{background:var(--tint);border-color:var(--tint-line);color:var(--accent-ink)}}

/* Tables read like Claude's: horizontal hairlines only, a firmer header rule
   and no filled header cell. */
.doc table{{border-collapse:collapse}}
.doc th,.doc td{{border:0;border-bottom:1px solid var(--hairline);
  padding:.6rem .8rem}}
.doc th{{background:none;color:var(--strong)}}
.doc thead th{{border-bottom:1px solid var(--line);font-weight:600}}
.doc tbody tr:last-child td{{border-bottom:0}}

/* ---- chrome ----------------------------------------------------------- */
/* Solid topbar: the translucent blur smeared over images as they scrolled
   underneath. The crumb drops the monospace for a quiet sans. */
.topbar{{background:var(--ground);backdrop-filter:none;
  -webkit-backdrop-filter:none;border-bottom:1px solid var(--hairline);
  height:3.4rem}}
.crumb{{font-family:var(--sans);font-size:.85rem;letter-spacing:0}}
.crumb b{{color:var(--strong)}}
.icon-btn:hover{{color:var(--accent-ink);background:var(--tint)}}

/* Sidebar: calmer and more generous. Group caps become title-case headings;
   the parent cap above the bucket subcaps is redundant and goes; the
   coverage meter is build telemetry, not reader wayfinding. */
.cov{{display:none}}
.side{{background:var(--surface)}}
.side-head{{padding:1.35rem 1.2rem 1.1rem}}
.brand b{{font-family:var(--sans);font-weight:600;letter-spacing:-.02em}}
.tree .tree-cap,.tree .tree-subcap{{font-size:.8rem;font-weight:650;
  letter-spacing:0;text-transform:capitalize;color:var(--strong);
  padding:1.5rem .6rem .45rem}}
.tree .tree-subcap{{text-transform:none}}
.tree .tree-cap:has(+ * .tree-subcap){{display:none}}
.doclink{{font-size:.9rem;padding:.5rem .7rem;gap:.6rem;
  transition:background .14s ease,color .14s ease}}
.item-g{{font-size:.88rem;padding:.42rem .65rem}}
.doclink.on,.item.on,.item-g.on{{background:var(--tint);color:var(--accent-ink)}}
.doclink.on svg{{color:var(--accent)}}
.side .search-btn{{background:var(--ground);border-color:var(--line);
  color:var(--faint);font-weight:500;animation:none;border-radius:10px}}
.side .search-btn:hover{{border-color:var(--tint-line);color:var(--muted);
  box-shadow:none}}
.side .search-btn kbd{{color:var(--faint);border-color:var(--line);
  background:var(--surface)}}

/* Right rail: a serif "On this page", larger targets, the accent marking
   the section actually on screen. */
.toc-cap{{font-family:var(--serif);font-size:1.02rem;font-weight:500;
  letter-spacing:0;text-transform:none;color:var(--strong);
  margin-bottom:.7rem}}
.toc-link{{font-size:.85rem;padding:.3rem .8rem}}
.toc-link.on{{color:var(--accent-ink);border-left-color:var(--accent)}}

/* The landing pill reads as a Claude-style eyebrow, not a bordered chip. */
.hero .pill{{border:none;background:none;padding:0;color:var(--accent-ink);
  font-size:.74rem;font-weight:650;letter-spacing:.13em;text-transform:uppercase}}
.stat-b span{{color:var(--faint)}}

/* Search palette: warmer surface, and the selected row states in accent. */
.palette{{background:color-mix(in srgb,var(--card) 94%,transparent);
  border-color:var(--line)}}
.res.sel{{background:var(--tint)}}
.res.sel .nm,.res.sel .kind{{color:var(--accent-ink)}}
.scrim{{background:color-mix(in srgb,var(--strong) 38%,transparent)}}

/* ---- layout repairs --------------------------------------------------- */
/* The article is pushed left of centre by the "On this page" rail, because
   the third grid column floors at the rail's own width. The hero band sits
   outside that grid and centred itself on the full width instead, so a page
   title and the first heading under it were ~88px out of line. Give the hero
   the same three columns whenever the rail is on the page. Below 1280px the
   rail is display:none and the base sheet pads both, so this is a
   wide-viewport repair only. */
@media (min-width:1280px){{
  .content:has(.toc-rail)>.hero-band>.hero-in:not(:has(.hero-grid)){{
    max-width:none;width:100%;display:grid;column-gap:2.75rem;
    grid-template-columns:1fr minmax(0,var(--article)) minmax(12.5rem,1fr)}}
  .content:has(.toc-rail)>.hero-band>.hero-in:not(:has(.hero-grid))>*{{
    grid-column:2}}
}}
/* The landing hero widens to 74rem, wider than the content column at common
   desktop sizes, and the base sheet only pads it below 1280px — so the
   terminal card ran flush into the right edge of the window. */
.hero-in:has(.hero-grid){{max-width:min(74rem,100%);
  padding-inline:clamp(1.3rem,3.2vw,3rem)}}

/* ---- motion and focus ------------------------------------------------- */
html{{scroll-behavior:smooth}}
:focus-visible{{outline:2px solid var(--accent);outline-offset:2px}}
::selection{{background:var(--tint);color:var(--strong)}}
@media (prefers-reduced-motion:reduce){{html{{scroll-behavior:auto}}}}

/* Scrollbars follow the palette in both schemes — the base sheet only ever
   dressed the dark one. */
*{{scrollbar-color:var(--scrollthumb) transparent}}
pre,.t-body{{scrollbar-width:thin;
  scrollbar-color:rgba(250,249,245,.18) transparent}}
pre::-webkit-scrollbar,.t-body::-webkit-scrollbar{{height:8px}}
pre::-webkit-scrollbar-thumb,.t-body::-webkit-scrollbar-thumb{{
  background:rgba(250,249,245,.16);border-radius:9px}}
.tree::-webkit-scrollbar-thumb{{background:var(--scrollthumb);
  border:2px solid var(--surface)}}
"""

# doxygen-awesome scopes dark under html.dark-mode plus the OS preference;
# the reference tree is built with HTML_COLORSTYLE=LIGHT, so the dark blocks
# only matter if that ever changes — kept for parity, they cost nothing.
API_DARK = """
  --primary-color:#d4a27f; --primary-dark-color:#d97757; --primary-light-color:#e4bfa2;
  --page-background-color:#1f1e1d; --page-foreground-color:#c2c0b6;
  --page-secondary-foreground-color:#a5a39a;
  --separator-color:#3a3833; --side-nav-background:#1b1a19; --side-nav-foreground:#c2c0b6;
  --header-background:#1f1e1d; --header-foreground:#c2c0b6;
  --searchbar-background:#262624;
  --code-background:#302f2c; --code-foreground:#c2c0b6;
  --fragment-background:#171614; --fragment-foreground:#d6d3c9;
  --odd-color:rgba(250,249,245,.04);
  --tablehead-background:#262624;
"""

API_THEME = f"""
{MARK}
html{{
  --primary-color:#a94c2d; --primary-dark-color:#9c4221; --primary-light-color:#c96442;
  --page-background-color:#faf9f5; --page-foreground-color:#3d3d3a;
  --page-secondary-foreground-color:#5c5b54;
  --separator-color:#e6e4da; --side-nav-background:#f5f3ec; --side-nav-foreground:#3d3d3a;
  --header-background:#faf9f5; --header-foreground:#3d3d3a;
  --searchbar-background:#ffffff;
  --code-background:#f0eee6; --code-foreground:#3d3d3a;
  --fragment-background:#1f1e1d; --fragment-foreground:#dedcd3;
  --odd-color:rgba(31,30,29,.04);
  --tablehead-background:#f0eee6;
  --content-line-height:1.72;
}}
html.dark-mode{{{API_DARK}}}
@media (prefers-color-scheme:dark){{html:not(.light-mode){{{API_DARK}}}}}
body,.title,.memname{{font-family:"Inter",-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}}
div.fragment,code,.memname,.paramname{{font-family:"JetBrains Mono",ui-monospace,"SF Mono",Menlo,monospace}}
.title,h1,h2.groupheader{{font-family:"Source Serif 4",Georgia,serif;font-weight:450;letter-spacing:0}}
"""


def theme(sheet: Path, css: str) -> str:
    """Prepend the font import and append the overrides; report what happened."""
    raw = sheet.read_text()
    if MARK in raw:
        return "already themed"
    sheet.write_text(FONT_IMPORT + raw + css)
    return "themed"


def main() -> int:
    """Apply CSS theme overrides (fonts and color scheme) to the main site shell and API reference tree if they exist; report what was themed."""
    did = []
    if SHEET.is_file():
        did.append(f"site shell {theme(SHEET, THEME)}")
    if API_SHEET.is_file():
        did.append(f"reference tree {theme(API_SHEET, API_THEME)}")
    if not did:
        print("    no rendered site — nothing to theme")
        return 0
    print(f"    {', '.join(did)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
