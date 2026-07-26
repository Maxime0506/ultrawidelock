#!/usr/bin/env python3
"""Make the architecture page's dependency graph legible.

The page generator emits the module import graph as one flat flowchart and a
zoomable shell around it. At this repo's size that renders as an unreadable
crop: dozens of modules, self-loop artifacts (a module importing its own
header), and a natural width several times the shell's, so the default 1:1
view shows two boxes and a tangle of splines.

This pass restructures the presentation, deriving everything from the page
itself so nothing is hand-curated to drift:

  * self-loop edges are dropped — at module level they are import artifacts,
    not information.
  * each module is assigned to its source directory, read from the page's own
    per-module headings, and the flat graph becomes clustered subgraphs.
  * a subsystem-level overview graph — one node per directory cluster, one
    arrow per aggregated dependency — goes above it. Small enough to be
    crisp at natural size, it answers the layering question at a glance.
  * every page with diagrams gets two script shims around the generator's
    nav.js: one tightens mermaid's layout spacing and bumps its font before
    the first render, the other clicks each diagram's own Fit control when
    the rendered graph overflows its shell, so big graphs open showing their
    whole shape instead of a random crop — and makes the shells direct:
    drag pans, cmd/ctrl+scroll (and trackpad pinch) zooms around the
    cursor, and plain or shift+scroll stays native, so the shell scrolls
    vertically or horizontally like any scrollable pane.
  * the per-module sections lose their visual noise: headings show the file
    name with the directory as a small eyebrow above it instead of one long
    path, the "depends on" rows become compact base-name chips (full path
    on hover) instead of comma-separated full paths, and the blurbs drop
    the "@file <name> — " prefix that would repeat the heading above them.
    The chip and prefix tidy also runs on every module reference page,
    whose "used by" rows and hero blurbs carry the same noise.
  * then the whole flat run of sections folds into one collapsed drill-down
    per directory cluster — color-dotted to match the graphs, a compact
    link row per module — so the page ends at a screenful instead of a
    hundred sections.
  * every graph gets a full-screen control: the wrap pins over the viewport
    with the same drag/zoom behavior, and Esc or the button collapses it.
  * a sitewide sidebar shim regroups the flat guide list under the same
    topic captions the landing page derives, and marks each reference
    directory group with its cluster's color dot from the graphs.

Idempotent for the same reason docs_media.py is: when the page generator is
not configured, the earlier passes run over a site/ kept from a previous
build, so a page may already carry the injections. Run from the repo root,
after docs_github.py and before the link pass.
"""

from __future__ import annotations

import html
import json
import os
import re
import sys
from pathlib import Path

SITE = Path("site")
ARCH = SITE / "architecture.html"

FIGURE_RE = re.compile(
    r'<figure class="graph-wrap(?: [^"]*)?"[^>]*><div class="graph-shell">'
    r'<pre class="mermaid">(.*?)</pre>.*?</figure>',
    re.S,
)
PATH_RE = re.compile(r"(?:modules|ports)/[\w./-]+\.(?:cpp|c|h)")
NAV_ANCHOR = '<script defer src="nav.js"></script>'

# Runs between mermaid.min.js and nav.js (deferred scripts execute in document
# order), so nav.js's own initialize call flows through the shim and the first
# render already uses the tightened layout.
PRE_SHIM = """<style>.graph-wrap figcaption{margin:.7rem 0 0;font-size:.82rem;color:var(--muted);text-align:center}
.gv-over .graph-tools{display:none}
.graph-shell{cursor:grab}
.graph-shell.dragging{cursor:grabbing;user-select:none}
.graph-wrap.gv-full{position:fixed;inset:0;z-index:70;margin:0;padding:.9rem 1.1rem .7rem;background:var(--surface);display:flex;flex-direction:column}
.graph-wrap.gv-full .graph-shell{flex:1;max-height:none}
.graph-wrap.gv-full figcaption{flex:none;margin:.6rem 0 0}
.gv-over.gv-full .graph-tools{display:flex}
body.gv-lock{overflow:hidden}
body.gv-lock .doc{animation:none}
.arch-sec h2 .arch-dir{display:block;font-size:.7rem;font-weight:500;color:var(--faint);font-family:var(--mono);letter-spacing:.04em;margin:0 0 .25rem}</style>
<script defer id="gv-pre">
(function(){if(!window.mermaid)return;var orig=mermaid.initialize.bind(mermaid);
mermaid.initialize=function(c){c=c||{};c.flowchart=Object.assign({},c.flowchart,{nodeSpacing:26,rankSpacing:40,padding:8});
if(c.themeVariables)c.themeVariables.fontSize="15px";orig(c)};})();
</script>"""

# Waits for nav.js to render the diagrams and attach the zoom tools, then
# presses Fit once on any graph wider than its shell, and wires the shells
# for direct manipulation: drag pans, cmd/ctrl+scroll (which is also what a
# trackpad pinch sends) zooms around the cursor keeping the zoom buttons'
# state in sync; any other wheel event is left to the browser, which
# scrolls the shell natively (shift+scroll horizontal) and chains to the
# page at its edges. Fail-soft: no diagrams, no tools, or no CDN and the
# interval just expires.
FIT_SHIM = """<script defer id="gv-fit">
(function(){
function gvFull(w,x){var shell=w.querySelector(".graph-shell"),svg=shell&&shell.querySelector("svg");
var on=!w.classList.contains("gv-full");
if(on){w.dataset.pk=w.dataset.zoom||"1";w.dataset.pl=shell.scrollLeft;w.dataset.pt=shell.scrollTop;
w.classList.add("gv-full");document.body.classList.add("gv-lock");
var f=w.querySelector(".graph-tools button[title='Fit to width']");if(f)f.click()}
else{w.classList.remove("gv-full");document.body.classList.remove("gv-lock");
if(svg&&w.dataset.pk){var nat=svg.getBoundingClientRect().width/parseFloat(w.dataset.zoom||"1");
svg.style.width=nat*w.dataset.pk+"px";w.dataset.zoom=w.dataset.pk;
shell.scrollLeft=w.dataset.pl;shell.scrollTop=w.dataset.pt}}
x.textContent=on?"\\u2715":"\\u2922";
x.title=on?"Exit full screen (esc)":"Full screen";x.setAttribute("aria-label",x.title)}
addEventListener("keydown",function(e){if(e.key!=="Escape")return;
var f=document.querySelector(".graph-wrap.gv-full");
if(f){var x=f.querySelector(".gv-x");if(x)x.click()}});
var n=0,t=setInterval(function(){n++;
var wraps=document.querySelectorAll(".graph-wrap");var done=0;
wraps.forEach(function(w){var svg=w.querySelector("svg"),shell=w.querySelector(".graph-shell"),
b=w.querySelector(".graph-tools button[title='Fit to width']");
if(svg&&b){done++;
if(!w.querySelector(".gv-x")){var x=document.createElement("button");
x.type="button";x.className="gv-x";x.textContent="\\u2922";x.title="Full screen";
x.setAttribute("aria-label","Full screen");x.onclick=function(){gvFull(w,x)};
b.parentNode.appendChild(x)}
if(!w.dataset.fitted){w.dataset.fitted=1;
var h=w.dataset.gvHome&&svg.querySelector('[id*="-'+w.dataset.gvHome+'-"]');
if(h){var hr=h.getBoundingClientRect(),sr=shell.getBoundingClientRect();
shell.scrollLeft+=hr.left-sr.left-48;
shell.scrollTop+=hr.top-sr.top-(shell.clientHeight-hr.height)/2}
else if(svg.getBoundingClientRect().width>shell.clientWidth)b.click()}}});
if(wraps.length&&done===wraps.length)clearInterval(t);else if(n>60)clearInterval(t)},120);
document.querySelectorAll(".graph-wrap").forEach(function(w){
var shell=w.querySelector(".graph-shell");if(!shell)return;
function setK(nk,cx,cy){var svg=shell.querySelector("svg");if(!svg)return;
var k=parseFloat(w.dataset.zoom||"1");nk=Math.max(.2,Math.min(2.5,nk));if(nk===k)return;
var r=shell.getBoundingClientRect();
var px=cx-r.left+shell.scrollLeft,py=cy-r.top+shell.scrollTop;
svg.style.width=svg.getBoundingClientRect().width/k*nk+"px";w.dataset.zoom=nk;
shell.scrollLeft=px*nk/k-(cx-r.left);shell.scrollTop=py*nk/k-(cy-r.top)}
shell.addEventListener("wheel",function(e){
if(!e.metaKey&&!e.ctrlKey)return;
e.preventDefault();var m=e.deltaMode===1?16:1;
setK(parseFloat(w.dataset.zoom||"1")*Math.pow(1.0015,-e.deltaY*m),e.clientX,e.clientY)},{passive:false});
var drag=null;
shell.addEventListener("mousedown",function(e){
if(e.button!==0||e.target.closest(".graph-tools"))return;
drag={x:e.clientX,y:e.clientY,l:shell.scrollLeft,t:shell.scrollTop};
shell.classList.add("dragging");e.preventDefault()});
addEventListener("mousemove",function(e){if(!drag)return;
shell.scrollLeft=drag.l-(e.clientX-drag.x);shell.scrollTop=drag.t-(e.clientY-drag.y)});
addEventListener("mouseup",function(){drag=null;shell.classList.remove("dragging")});
});})();
</script>"""

OVER_CAPTION = (
    "Subsystem-level view of the module graph below: each box is a source "
    "directory, each arrow an import dependency between them. The colors "
    "follow each directory through the detail graph and the sections below."
)
DETAIL_CAPTION = (
    "Every module and its imports, grouped and color-keyed by directory. "
    "Opens on the entry point; drag to pan, &#8984;&#8202;scroll or pinch "
    "to zoom, &#10530; for full screen, or use the per-module sections below."
)

# Categorical hues for the directory clusters, one slot per cluster in fixed
# (sorted-name) order. Light and dark are the same hues stepped per surface;
# the set passes the CVD/normal-vision gates on both of this site's surfaces,
# and every colored mark also carries its name as text, so color is never the
# only channel. Node text keeps the theme ink — the border and wash carry
# identity.
PALETTE = (
    ("#2a78d6", "#3987e5"),  # blue
    ("#eb6834", "#d95926"),  # orange
    ("#1baf7a", "#199e70"),  # aqua
    ("#eda100", "#c98500"),  # yellow
    ("#e87ba4", "#d55181"),  # magenta
    ("#008300", "#008300"),  # green
    ("#4a3aa7", "#9085e9"),  # violet
    ("#e34948", "#e66767"),  # red
)


def parse_edges(mermaid: str) -> tuple[str, list[tuple[str, str]]]:
    """Parse a mermaid graph block into its header line and a list of (source, target) node pairs, dropping self-loops."""
    lines = mermaid.strip().split("\n")
    pairs = []
    for line in lines[1:]:
        a, _, b = line.strip().partition(" --> ")
        if a and b and a != b:  # drops the self-loop artifacts
            pairs.append((a, b))
    return lines[0], pairs


def stem_dirs(page: str) -> dict[str, str]:
    """module name -> its source directory, from the page's file headings.

    A stem can exist both in modules/ and in a port's copy; the shared core
    is the one the import graph describes, so modules/ wins.
    """
    out: dict[str, str] = {}
    for p in PATH_RE.findall(page):
        d, f = p.rsplit("/", 1)
        stem = re.sub(r"\.(cpp|c|h)$", "", f)
        if stem not in out or (
            d.startswith("modules/") and not out[stem].startswith("modules/")
        ):
            out[stem] = d
    return out


def cluster_of(directory: str) -> str:
    """Return the top-level cluster name for a source directory: woz_uwb submodule names become "woz_uwb/submodule", others return their first path component."""
    d = re.sub(r"^modules/", "", directory)
    m = re.match(r"woz_uwb/src/([a-z_]+)", d)
    return "woz_uwb/" + m.group(1) if m else d.split("/")[0]


def color_css(names: list[str], clusters: dict[str, set[str]]) -> str:
    """Theme-aware cluster colors: tinted node fills, hue strokes, dot vars.

    Selectors go by mermaid's DOM ids (flowchart-<node>-<n>), with !important
    to beat the svg's own id-prefixed stylesheet; the theme toggle scope must
    win over the OS preference in both directions.
    """
    light = ";".join(f"--gv{i}:{PALETTE[i % 8][0]}" for i in range(len(names)))
    dark = ";".join(f"--gv{i}:{PALETTE[i % 8][1]}" for i in range(len(names)))
    rules = [
        f":root{{{light}}}",
        f':root[data-theme="dark"]{{{dark}}}',
        f'@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{{dark}}}}}',
        ".gv-dot{display:inline-block;width:.55em;height:.55em;"
        "border-radius:50%;background:var(--c);margin-right:.4em}",
    ]
    for i, c in enumerate(names):
        node_sel = ",".join(
            f'.graph-shell .node[id*="-{n}-"] rect' for n in sorted(clusters[c])
        )
        rules.append(
            f'{node_sel},.graph-shell .node[id*="-gvn{i}-"] rect'
            f"{{stroke:var(--gv{i})!important;"
            f"fill:color-mix(in srgb,var(--gv{i}) 10%,transparent)!important}}"
        )
        rules.append(
            f'.graph-shell .cluster[id*="gvc{i}"] rect'
            f"{{stroke:color-mix(in srgb,var(--gv{i}) 45%,transparent)!important;"
            f"fill:color-mix(in srgb,var(--gv{i}) 6%,transparent)!important}}"
        )
    return "<style>" + "\n".join(rules) + "</style>"


def figures(page: str, mermaid: str) -> tuple[str, dict[str, int]]:
    """Build the architecture overview and clustered detail graphs from a mermaid definition, returning the HTML block and a cluster-to-color-slot mapping."""
    header, pairs = parse_edges(mermaid)
    dirs = stem_dirs(page)
    clusters: dict[str, set[str]] = {}
    loose: set[str] = set()
    for a, b in pairs:
        for n in (a, b):
            if n in dirs:
                clusters.setdefault(cluster_of(dirs[n]), set()).add(n)
            else:
                loose.add(n)
    if loose:
        print(
            f"    {len(loose)} module(s) without a directory heading kept "
            f"ungrouped: {', '.join(sorted(loose))}"
        )

    def cluster_key(n: str) -> str | None:
        """Map a node name to its cluster, or None if the node has no directory heading on the page."""
        return cluster_of(dirs[n]) if n in dirs else None

    cedges = sorted(
        {
            (cluster_key(a), cluster_key(b))
            for a, b in pairs
            if cluster_key(a) and cluster_key(b) and cluster_key(a) != cluster_key(b)
        }
    )
    names = sorted(clusters)
    slots = {c: i for i, c in enumerate(names)}

    # the default view anchors on the graph's entry point: nothing imports
    # it, and among those the one that drives the most modules
    targets = {b for _, b in pairs}
    outdeg: dict[str, int] = {}
    for a, _ in pairs:
        outdeg[a] = outdeg.get(a, 0) + 1
    entries = sorted({n for p in pairs for n in p} - targets)
    home = max(entries, key=lambda n: outdeg.get(n, 0)) if entries else ""

    cid = {c: f"gvn{i}" for i, c in enumerate(names)}
    over = (
        [header]
        + [f'  {cid[c]}["{c}"]' for c in names]
        + [f"  {cid[a]} --> {cid[b]}" for a, b in cedges]
    )
    detail = [header]
    for i, c in enumerate(names):
        detail.append(f'  subgraph gvc{i}["{c}"]')
        detail += [f"    {n}" for n in sorted(clusters[c])]
        detail.append("  end")
    detail += [f"  {a} --> {b}" for a, b in pairs]

    esc = lambda t: html.escape("\n".join(t), quote=False)
    home_attr = f' data-gv-home="{home}"' if home else ""
    slot_json = json.dumps({c: i for c, i in slots.items()}, sort_keys=True)
    block = (
        # the cluster -> color-slot map, kept machine-readable so a rerun
        # over an already-restructured page can rebuild the sidebar shim
        f'<script type="application/json" id="gv-slots">{slot_json}</script>\n'
        f"{color_css(names, clusters)}\n"
        f'<figure class="graph-wrap gv-over"><div class="graph-shell">'
        f'<pre class="mermaid">{esc(over)}</pre></div>\n'
        f"<figcaption>{OVER_CAPTION}</figcaption></figure>\n"
        f'<figure class="graph-wrap"{home_attr}><div class="graph-shell">'
        f'<pre class="mermaid">{esc(detail)}</pre></div>\n'
        f"<figcaption>{DETAIL_CAPTION}</figcaption></figure>"
    )
    return block, slots


HEAD_RE = re.compile(
    r'<h2><a href="([^"]+)"><code>([\w./-]+/)([\w.-]+)</code></a></h2>'
)
CHIP_RE = re.compile(r'<a href="([^"]+)"><code>([\w./-]+)</code></a>(?:,\s*)?')
CHIPS_BLOCK_RE = re.compile(
    r'(<p class="chips">(?:depends on|used by) )(.*?)(</p>)', re.S
)
ATFILE_RE = re.compile(r"<p>@file [\w.-]+ — ")
ATFILE_BARE_RE = re.compile(r"<p>@file [\w.-]+</p>\n?")
LEDE_RE = re.compile(r'(<p class="lede">)@file [\w.-]+ — ')
LEDE_BARE_RE = re.compile(r'<p class="lede">@file [\w.-]+(?:…|\.{3})?</p>\n?')


def chip(m: re.Match) -> str:
    """Format a single chip reference as an HTML link with code formatting, extracting the basename from the full path."""
    path = m.group(2)
    return (
        f'<a href="{m.group(1)}" title="{path}">'
        f'<code>{path.rsplit("/", 1)[-1]}</code></a> '
    )


def chips_block(m: re.Match) -> str:
    """Rewrite a block of chip references by applying link and formatting to each one, preserving leading and trailing whitespace."""
    return m.group(1) + CHIP_RE.sub(chip, m.group(2)).rstrip() + m.group(3)


def tidy_page(page: str) -> str:
    """The same de-noising the architecture sections get, on a module page:
    base-name chips with the full path on hover, and no "@file <name> — "
    prefix repeating the file name the hero already shows."""
    page = CHIPS_BLOCK_RE.sub(chips_block, page)
    page = LEDE_RE.sub(r"\1", page)
    page = LEDE_BARE_RE.sub("", page)
    page = ATFILE_RE.sub("<p>", page)
    return ATFILE_BARE_RE.sub("", page)


def tidy_sections(page: str, slots: dict[str, int]) -> tuple[str, int, int]:
    """Short file-name headings with a directory eyebrow; base-name chips.

    Directories that form a cluster in the graph get its color dot in the
    eyebrow, correlating each section with the graphs above.
    """

    def head(m: re.Match) -> str:
        """Rewrite a module section heading with a shortened directory name, optional color-slot dot, and a hyperlinked source filename."""
        d = m.group(2).rstrip("/")
        i = slots.get(cluster_of(d))
        dot = (
            f'<i class="gv-dot" style="--c:var(--gv{i})"></i>'
            if i is not None
            else ""
        )
        return (
            f'<h2><span class="arch-dir">{dot}{d}</span>'
            f'<a href="{m.group(1)}"><code>{m.group(3)}</code></a></h2>'
        )

    page, heads = HEAD_RE.subn(head, page)
    page, blocks = CHIPS_BLOCK_RE.subn(chips_block, page)
    # with short headings, a blurb's "@file <name> — " prefix just repeats
    # the heading directly above it
    page = ATFILE_RE.sub("<p>", page)
    page = ATFILE_BARE_RE.sub("", page)
    return page, heads, blocks


# A tidied per-module section: dot slot (optional), directory eyebrow,
# reference link, file name, then the body up to the section close.
SEC_PARSE = re.compile(
    r'<section class="arch-sec"><h2><span class="arch-dir">'
    r'(?:<i class="gv-dot" style="--c:var\(--gv(\d+)\)"></i>)?([\w./-]+)</span>'
    r'<a href="([^"]+)"><code>([\w.-]+)</code></a></h2>\n(.*?)</section>\n?',
    re.S,
)
BLURB_RE = re.compile(r"<p>(.*?)</p>", re.S)

GRP_CSS = """<style>
.arch-grp{border:1px solid var(--line);border-radius:13px;background:var(--card);margin:.7rem 0;transition:border-color .15s}
.arch-grp:hover,.arch-grp[open]{border-color:var(--tint-line)}
.arch-grp summary{display:flex;align-items:center;gap:.6rem;padding:.75rem 1rem;cursor:pointer;list-style:none}
.arch-grp summary::-webkit-details-marker{display:none}
.arch-grp .ag-name{font-family:var(--mono);font-weight:650;font-size:.92rem}
.arch-grp .ag-dir{font-family:var(--mono);font-size:.72rem;color:var(--faint)}
.arch-grp .ag-n{margin-left:auto;font-size:.72rem;color:var(--muted);font-variant-numeric:tabular-nums}
.arch-grp .ag-chev{color:var(--faint);transition:transform .2s;line-height:.6}
.arch-grp[open] .ag-chev{transform:rotate(180deg)}
.arch-grp .ag-body{padding:.1rem 1rem .8rem;border-top:1px solid var(--hairline)}
.arch-grp[open] .ag-body{animation:ag-in .25s cubic-bezier(.2,.7,.2,1) both}
@keyframes ag-in{from{opacity:0;transform:translateY(-4px)}}
.arch-grp .rows{margin:.3rem 0 0}
.arch-grp .row-name code{background:none;border:0;padding:0;font-size:.88rem}
.arch-grp .row-desc{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
</style>"""


def group_sections(page: str, slots: dict[str, int]) -> tuple[str, int]:
    """Fold the flat run of per-module sections into per-cluster drill-downs.

    One collapsed group per directory cluster, in the graphs' color order,
    each module a compact link row (name + clamped blurb). Dependency rows
    are dropped here — the graph above carries them, and every module page
    keeps its own. The group holding the graphs' entry point opens by
    default.
    """
    secs: list[tuple[str, str, str, str]] = []

    def eat(m: re.Match) -> str:
        """Extract a section heading metadata triple and blurb into the accumulator, returning empty string to erase the matched text."""
        b = BLURB_RE.search(m.group(5))
        secs.append(
            (m.group(2), m.group(3), m.group(4), b.group(1).strip() if b else "")
        )
        return ""

    page = SEC_PARSE.sub(eat, page)
    if not secs:
        return page, 0

    groups: dict[str, list[tuple[str, str, str, str]]] = {}
    for d, href, name, blurb in secs:
        groups.setdefault(cluster_of(d), []).append((d, href, name, blurb))

    hm = re.search(r' data-gv-home="([\w.-]+)"', page)
    home = hm.group(1) if hm else ""
    order = sorted(groups, key=lambda c: (slots.get(c, len(slots)), c))
    out = [GRP_CSS, '<div class="section-h"><h2>Modules</h2><span class="rule"></span></div>']
    for c in order:
        mods = groups[c]
        i = slots.get(c)
        dot = f'<i class="gv-dot" style="--c:var(--gv{i})"></i>' if i is not None else ""
        prefix = os.path.commonprefix([d for d, _, _, _ in mods]).rstrip("/")
        is_home = any(re.sub(r"\.(cpp|c|h)$", "", n) == home for _, _, n, _ in mods)
        rows = "\n".join(
            f'<li><a href="{href}" title="{d}/{name}">'
            f'<span class="row-name"><code>{name}</code></span>'
            f'<span class="row-desc">{blurb}</span></a></li>'
            for d, href, name, blurb in mods
        )
        out.append(
            f'<details class="arch-grp"{" open" if is_home else ""}>'
            f"<summary>{dot}"
            f'<span class="ag-name">{c}</span>'
            f'<span class="ag-dir">{prefix}</span>'
            f'<span class="ag-n">{len(mods)} module{"s" if len(mods) != 1 else ""}</span>'
            f'<span class="ag-chev">&#8964;</span></summary>'
            f'<div class="ag-body"><ul class="rows">\n{rows}\n</ul></div></details>'
        )
    page = page.replace('<div class="arch">', '<div class="arch">\n' + "\n".join(out), 1)
    return page, len(secs)


SLOTS_RE = re.compile(
    r'<script type="application/json" id="gv-slots">(\{.*?\})</script>'
)
BUCKET_RE = re.compile(
    r'<div class="row-cap">([^<]+)</div>\s*<ul class="rows">(.*?)</ul>', re.S
)
ROW_HREF_RE = re.compile(r'<li><a href="([\w.-]+)\.html"')

# Runs after nav.js has built the sidebar tree: regroups the flat guide list
# under the same topic captions the landing page renders, and marks each
# reference directory group with its graph cluster's color dot. The color
# variables are re-declared here because the graph page's copy only ships
# with the architecture page. Data is baked in at build time (__SLOT__,
# __BUCKETS__, __VARS__); lookups that miss are simply skipped.
SIDE_TMPL = """<style>:root{__LIGHT__}
:root[data-theme="dark"]{__DARK__}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){__DARK__}}
.gv-dot{display:inline-block;width:.55em;height:.55em;border-radius:50%;background:var(--c);margin-right:.4em}
.tree-subcap{padding:.85rem .6rem .15rem;font-size:.6rem;font-weight:650;letter-spacing:.12em;text-transform:uppercase;color:var(--faint)}
.group-h .gv-dot{width:.5em;height:.5em;flex:none}</style>
<script id="gv-side">
(function(){function go(){var tree=document.getElementById("tree");if(!tree)return;
var SLOT=__SLOT__,BUCKETS=__BUCKETS__;
function clus(d){d=d.replace(/^modules\\//,"");
var m=d.match(/^woz_uwb\\/src\\/([a-z_]+)/);return m?"woz_uwb/"+m[1]:d.split("/")[0]}
var cap="";
Array.prototype.slice.call(tree.children).forEach(function(el){
if(el.classList.contains("tree-cap")){cap=el.textContent;return}
if(!el.classList.contains("group"))return;
var h=el.querySelector(".group-h");if(!h||h.querySelector(".gv-dot"))return;
var rest=h.textContent.replace(/^\\s*\\u2304\\s*/,"").trim();
var dir=(cap==="repository"?"":cap+"/")+rest;
var s=SLOT[clus(dir)];if(s==null)return;
var d=document.createElement("i");d.className="gv-dot";
d.style.setProperty("--c","var(--gv"+s+")");
h.insertBefore(d,h.childNodes[1]||null)});
var g=tree.querySelector(".item-g"),gbox=g&&g.parentElement;
if(gbox&&BUCKETS.length){var bySlug={};
Array.prototype.slice.call(gbox.children).forEach(function(a){
var m=(a.getAttribute("href")||"").match(/^([\\w.-]+)\\.html/);if(m)bySlug[m[1]]=a});
var frag=document.createDocumentFragment(),used={};
BUCKETS.forEach(function(b){
var hit=b[1].filter(function(s){return bySlug[s]});if(!hit.length)return;
var c=document.createElement("div");c.className="tree-subcap";c.textContent=b[0];
frag.appendChild(c);
hit.forEach(function(s){frag.appendChild(bySlug[s]);used[s]=1})});
var left=Object.keys(bySlug).filter(function(s){return !used[s]});
if(left.length){var c=document.createElement("div");c.className="tree-subcap";
c.textContent="More";frag.appendChild(c);
left.forEach(function(s){frag.appendChild(bySlug[s])})}
gbox.textContent="";gbox.appendChild(frag)}}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go()})();
</script>"""


def side_shim(slots: dict[str, int], buckets: list[tuple[str, list[str]]]) -> str:
    """Generate the sidebar shim HTML with color-slot CSS variables and JSON-encoded cluster/bucket metadata for the interactive architecture sidebar."""
    light = ";".join(f"--gv{i}:{PALETTE[i % 8][0]}" for i in range(len(slots)))
    dark = ";".join(f"--gv{i}:{PALETTE[i % 8][1]}" for i in range(len(slots)))
    return (
        SIDE_TMPL.replace("__LIGHT__", light)
        .replace("__DARK__", dark)
        .replace("__SLOT__", json.dumps(slots, sort_keys=True))
        .replace("__BUCKETS__", json.dumps(buckets))
    )


def main() -> int:
    """Restructure the rendered architecture page: split its graph into overview and detail, shorten module headings, compact dependency rows, group sections by cluster, and apply layout shimming and sidebar injection to all HTML files."""
    if not ARCH.is_file():
        print("    no rendered site — nothing to restructure")
        return 0

    page = ARCH.read_bytes().decode()
    dirty = False
    slots: dict[str, int] = {}
    if 'class="graph-wrap gv-over"' in page:
        print("    architecture graphs already restructured")
        m = SLOTS_RE.search(page)
        if m:
            slots = json.loads(m.group(1))
    else:
        m = FIGURE_RE.search(page)
        if not m:
            print(
                "docs_graph: architecture page has no graph figure to "
                "restructure — generator layout changed?",
                file=sys.stderr,
            )
            return 1
        figs, slots = figures(page, html.unescape(m.group(1)))
        page = page[: m.start()] + figs + page[m.end() :]
        dirty = True
        print("    architecture graph split into overview + clustered detail")

    if 'class="arch-dir"' in page:
        print("    module sections already tidied")
    else:
        page, heads, blocks = tidy_sections(page, slots)
        if not heads and not blocks:
            print(
                "docs_graph: architecture page has no module sections to "
                "tidy — generator layout changed?",
                file=sys.stderr,
            )
            return 1
        dirty = True
        print(
            f"    {heads} section heading(s) shortened, "
            f"{blocks} depends-on row(s) compacted"
        )

    if 'class="arch-grp"' in page:
        print("    module sections already grouped")
    else:
        page, n = group_sections(page, slots)
        if not n:
            print(
                "docs_graph: architecture page has no module sections to "
                "group — generator layout changed?",
                file=sys.stderr,
            )
            return 1
        dirty = True
        print(f"    {n} module section(s) folded into cluster drill-downs")
    if dirty:
        ARCH.write_bytes(page.encode())

    # sidebar shim data: the guide topics come from the landing page's own
    # captioned guide groups, so the grouping never drifts from it
    buckets: list[tuple[str, list[str]]] = []
    index = SITE / "index.html"
    if index.is_file():
        for m in BUCKET_RE.finditer(index.read_text()):
            slugs = ROW_HREF_RE.findall(m.group(2))
            if slugs:
                buckets.append((m.group(1), slugs))
    side = side_shim(slots, buckets).encode()

    shimmed = kept = tidied = sided = 0
    anchor = NAV_ANCHOR.encode()
    for p in sorted(SITE.glob("*.html")):
        content = p.read_bytes()
        orig = content
        if p != ARCH:
            text = content.decode()
            t = tidy_page(text)
            if t != text:
                tidied += 1
                content = t.encode()
        if b'class="mermaid"' in content:
            if b'id="gv-pre"' in content:
                kept += 1
            elif anchor in content:
                content = content.replace(
                    anchor, PRE_SHIM.encode() + anchor + FIT_SHIM.encode(), 1
                )
                shimmed += 1
        if b'id="gv-side"' not in content and anchor in content:
            content = content.replace(anchor, anchor + side, 1)
            sided += 1
        if content != orig:
            p.write_bytes(content)
    note = f" ({kept} already shimmed)" if kept else ""
    print(f"    layout + auto-fit shims on {shimmed} page(s){note}")
    print(f"    {tidied} page(s) de-noised, sidebar shim on {sided} page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
