#!/usr/bin/env python3
"""Render the subsystem graph as a standalone page.

Input is graphify-out/graph.json, which graphify builds from the source tree:

    graphify . --update --code-only

That directory is gitignored and the tool is not in this repository, so the
graph is an enrichment: when the file is absent the site build skips this page
and says so, exactly as it does when emscripten is missing.

The presentation follows what v0.3.0's docs_graph.py established, in this
design system's colours rather than the warm-paper ones it was written for:

  * self-loops are dropped -- at subsystem level they are import artifacts
  * nodes are clustered by the directory that owns them, and the cluster is
    drawn as a container rather than left implicit
  * a subsystem-level overview sits above the detail. Small enough to be crisp
    at natural size, it answers the layering question at a glance
  * each cluster carries a categorical hue, and every coloured mark also
    carries its name as text, so colour is never the only channel. The hues
    are the design system's own syntax palette, which is already a spread
    tuned for this surface
  * node text keeps the theme ink; the border and wash carry identity
  * drag pans, cmd/ctrl+scroll and pinch zoom around the cursor, and the graph
    opens fitted rather than cropped

What is drawn is the CORE only -- modules, ports and applications. Tests,
tooling and build glue reference every module and flatten the layering into a
hairball, which hides the one thing the graph exists to show.
"""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

CORE_TOP = ("modules", "ports", "apps")
VENDORED = ("dwt_uwb_driver", "detools")

TIERS = ["apps", "ports", "modules"]
TIER_CAPTION = {
    "apps": "applications",
    "ports": "platform backends",
    "modules": "portable protocol",
}

# The design system's syntax palette: a categorical spread already tuned for
# this surface. One slot per cluster, assigned in sorted order so the mapping
# is stable across builds.
HUES = ["var(--syn-keyword)", "var(--syn-string)", "var(--syn-number)",
        "var(--syn-const)", "var(--syn-macro)", "var(--syn-func)",
        "var(--syn-type)"]

W = 1240
NODE_H, NODE_GAP = 46, 16
TIER_TOP, TIER_H = 132, 210
PAD_X = 34


def subsystem(path: str) -> str | None:
    if not path:
        return None
    parts = path.split("/")
    if len(parts) < 2 or parts[0] not in CORE_TOP:
        return None
    if any(v in parts for v in VENDORED):
        return None
    if parts[1].endswith(".md"):
        return None
    return f"{parts[0]}/{parts[1]}"


# Below this many references an edge is usually a single include: real, but not
# structure. Drawing all 50 put 49 crossings on the page and buried the shape;
# the 18 edges under this line carry about 5% of the total references between
# them. The page says how many are hidden rather than quietly dropping them.
MIN_EDGE = 5


def aggregate(graph: dict) -> tuple[Counter, Counter]:
    owner: dict[str, str] = {}
    for node in graph["nodes"]:
        name = subsystem(node.get("source_file", ""))
        if name:
            owner[node["id"]] = name
    sizes: Counter = Counter(owner.values())
    edges: Counter = Counter()
    for link in graph["links"]:
        a, b = owner.get(link["source"]), owner.get(link["target"])
        if a and b and a != b:
            edges[(a, b) if a < b else (b, a)] += 1
    return sizes, edges


def count_crossings(tiers: dict[str, list[str]], edges: Counter) -> int:
    """Edges between the same pair of tiers cross when their x-order flips."""
    total = 0
    for above, below in zip(TIERS, TIERS[1:]):
        ia = {n: i for i, n in enumerate(tiers[above])}
        ib = {n: i for i, n in enumerate(tiers[below])}
        pairs = []
        for (a, b) in edges:
            if a in ia and b in ib:
                pairs.append((ia[a], ib[b]))
            elif b in ia and a in ib:
                pairs.append((ia[b], ib[a]))
        for i in range(len(pairs)):
            for j in range(i + 1, len(pairs)):
                if (pairs[i][0] - pairs[j][0]) * (pairs[i][1] - pairs[j][1]) < 0:
                    total += 1
    return total


def order_tiers(sizes: Counter, edges: Counter) -> dict[str, list[str]]:
    """Barycenter ordering, swept both ways, keeping the fewest crossings.

    A single downward sweep leaves the bottom tier ordered for its parents but
    never reconsiders the tiers above it, which on this graph left roughly
    twice the crossings. Sweeping up as well and keeping the best arrangement
    seen is still deterministic, and cheap at 17 nodes.
    """
    adj: dict[str, list[str]] = defaultdict(list)
    for (a, b), weight in edges.items():
        adj[a] += [b] * weight
        adj[b] += [a] * weight

    tiers = {t: sorted(n for n in sizes if n.startswith(t + "/")) for t in TIERS}
    degree = {n: len(adj[n]) for n in sizes}
    top = sorted(tiers[TIERS[0]], key=lambda n: (-degree[n], n))
    centred: list[str] = []
    for i, name in enumerate(top):
        centred.insert(0, name) if i % 2 else centred.append(name)
    tiers[TIERS[0]] = centred

    def sweep(fixed: str, moving: str) -> None:
        index = {n: i for i, n in enumerate(tiers[fixed])}
        span = max(len(index) - 1, 1)

        def bary(node: str) -> float:
            near = [index[m] for m in adj[node] if m in index]
            return sum(near) / len(near) if near else span / 2

        tiers[moving] = sorted(tiers[moving], key=lambda n: (bary(n), n))

    best = {t: list(v) for t, v in tiers.items()}
    best_score = count_crossings(tiers, edges)
    for step in range(8):
        pairs = list(zip(TIERS, TIERS[1:]))
        for above, below in (pairs if step % 2 == 0 else reversed(pairs)):
            sweep(above, below) if step % 2 == 0 else sweep(below, above)
        score = count_crossings(tiers, edges)
        if score < best_score:
            best_score, best = score, {t: list(v) for t, v in tiers.items()}
    return best


def label_of(name: str) -> str:
    return name.split("/", 1)[1].replace("ultrawidelock_", "")


def esc(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def layout(tiers: dict[str, list[str]]) -> tuple[dict, int, int]:
    """Node boxes, sized to their label, spread evenly across each tier.

    The canvas widens to whatever the busiest tier needs rather than clipping
    it: ten modules do not fit the nominal width, and a tier running off the
    right edge is the one failure a crossing count cannot see. Pan and zoom
    make the extra width cost nothing.
    """
    widths = {t: [max(104.0, 26 + len(label_of(n)) * 8.6) for n in tiers[t]]
              for t in TIERS}
    spans = {t: sum(w) + NODE_GAP * max(len(w) - 1, 0) for t, w in widths.items()}
    canvas = max(W, max(spans.values()) + 2 * PAD_X)

    placed: dict[str, tuple[float, float, float]] = {}
    for row, tier in enumerate(TIERS):
        if not tiers[tier]:
            continue
        x = (canvas - spans[tier]) / 2
        y = TIER_TOP + row * TIER_H
        for name, width in zip(tiers[tier], widths[tier]):
            placed[name] = (x + width / 2, y, width)
            x += width + NODE_GAP
    height = TIER_TOP + (len(TIERS) - 1) * TIER_H + 96
    return placed, height, int(canvas)


def svg(sizes: Counter, edges: Counter, hue_of: dict[str, str]) -> tuple[str, int]:
    tiers = order_tiers(sizes, edges)
    pos, height, canvas = layout(tiers)
    heaviest = max(edges.values()) if edges else 1
    out = [f'<svg id="g" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {canvas} {height}" '
           f'role="img" aria-label="Subsystem dependency graph: applications over '
           f'platform backends over the portable protocol">']
    caps: list[str] = []

    # Cluster bands, drawn behind everything.
    for row, tier in enumerate(TIERS):
        members = tiers[tier]
        if not members:
            continue
        xs = [pos[n][0] - pos[n][2] / 2 for n in members]
        xe = [pos[n][0] + pos[n][2] / 2 for n in members]
        y = TIER_TOP + row * TIER_H
        x0, x1 = min(xs) - 20, max(xe) + 20
        out.append(f'<rect class="band" x="{x0:.0f}" y="{y - NODE_H / 2 - 24:.0f}" '
                   f'width="{x1 - x0:.0f}" height="{NODE_H + 48}" rx="14"/>')
        # Captions are held back and drawn last: edges cross the band, and a
        # line through the word is the difference between a label and noise.
        caps.append(f'<text class="band-cap" x="{x0 + 14:.0f}" '
                    f'y="{y - NODE_H / 2 - 6:.0f}">{TIER_CAPTION[tier]}</text>')

    # Edges under the nodes so no line crosses a label.
    for (a, b), weight in sorted(edges.items(), key=lambda kv: kv[1]):
        if a not in pos or b not in pos:
            continue
        (x1, y1, _), (x2, y2, w2) = pos[a], pos[b]
        share = weight / heaviest
        if y1 == y2:
            lift = y1 - NODE_H / 2 - 16
            path = (f"M{x1:.1f},{y1 - NODE_H / 2:.0f} "
                    f"Q{(x1 + x2) / 2:.1f},{lift - 26:.0f} {x2:.1f},{y2 - NODE_H / 2:.0f}")
        else:
            top, bot = (y1, y2) if y1 < y2 else (y2, y1)
            xt, xb = (x1, x2) if y1 < y2 else (x2, x1)
            path = (f"M{xt:.1f},{top + NODE_H / 2:.0f} "
                    f"C{xt:.1f},{top + NODE_H / 2 + 62:.0f} "
                    f"{xb:.1f},{bot - NODE_H / 2 - 62:.0f} {xb:.1f},{bot - NODE_H / 2:.0f}")
        out.append(f'<path class="edge" d="{path}" stroke="{hue_of[a]}" '
                   f'stroke-width="{0.9 + 3.4 * share:.2f}" '
                   f'opacity="{0.22 + 0.48 * share:.2f}"><title>{esc(a)} &#8596; '
                   f'{esc(b)} &#183; {weight} references</title></path>')

    for name, (x, y, width) in pos.items():
        hue = hue_of[name]
        out.append(
            f'<g class="node"><rect x="{x - width / 2:.1f}" y="{y - NODE_H / 2:.0f}" '
            f'width="{width:.1f}" height="{NODE_H}" rx="9" stroke="{hue}"/>'
            f'<rect class="chip" x="{x - width / 2:.1f}" y="{y - NODE_H / 2:.0f}" '
            f'width="4" height="{NODE_H}" fill="{hue}"/>'
            f'<text x="{x:.1f}" y="{y - 1:.1f}">{esc(label_of(name))}</text>'
            f'<text class="count" x="{x:.1f}" y="{y + 14:.1f}">{sizes[name]} files</text>'
            f'<title>{esc(name)} &#183; {sizes[name]} files</title></g>')

    out.extend(caps)
    out.append("</svg>")
    return "\n".join(out), height


def overview(sizes: Counter, edges: Counter) -> str:
    """One box per tier, one arrow per aggregated dependency. Answers the
    layering question before the detail below is read at all."""
    totals = Counter()
    between: Counter = Counter()
    for name, n in sizes.items():
        totals[name.split("/")[0]] += n
    for (a, b), weight in edges.items():
        ta, tb = a.split("/")[0], b.split("/")[0]
        if ta != tb:
            between[(ta, tb) if TIERS.index(ta) < TIERS.index(tb) else (tb, ta)] += weight

    rows = []
    for tier in TIERS:
        rows.append(
            f'<div class="ov-tier"><b>{TIER_CAPTION[tier]}</b>'
            f'<span>{len([n for n in sizes if n.startswith(tier + "/")])} subsystems '
            f'&#183; {totals[tier]} files</span></div>')
    links = " ".join(
        f'<span class="ov-link">{TIER_CAPTION[a]} &#8596; {TIER_CAPTION[b]} '
        f'<b>{w}</b></span>' for (a, b), w in between.most_common())
    return ('<div class="ov">' + "".join(rows) + '</div>'
            f'<div class="ov-links">{links}</div>')


PAGE = """<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Subsystem graph — UltraWideLock</title>
<script>(function(){try{var t=localStorage.getItem('uwl-theme');if(t)\
document.documentElement.setAttribute('data-theme',t);}catch(e){}})();</script>
<link rel="stylesheet" href="../assets/design/styles.css">
<style>
  .shell{position:relative;background:var(--card);border:1px solid var(--line);
    border-radius:var(--r-lg);box-shadow:var(--shadow-1);overflow:hidden;
    cursor:grab;touch-action:none}
  .shell.grabbing{cursor:grabbing}
  .shell.full{position:fixed;inset:0;z-index:60;border-radius:0;margin:0}
  .pan{transform-origin:0 0}
  .tools{position:absolute;top:10px;right:10px;display:flex;gap:6px;z-index:2}
  .tools button{font:500 12px var(--font-mono);color:var(--ink);
    background:var(--surface);border:1px solid var(--line);border-radius:var(--r-sm);
    padding:5px 9px;cursor:pointer}
  .tools button:hover{border-color:var(--accent-line);color:var(--strong)}
  .band{fill:var(--sunken);stroke:var(--hairline)}
  .band-cap{fill:var(--faint);font:500 10.5px var(--font-mono);letter-spacing:.09em;
    text-transform:uppercase}
  .edge{fill:none;stroke-linecap:round}
  .node rect{fill:var(--surface);stroke-width:1.25}
  .node .chip{stroke:none}
  .node text{fill:var(--strong);font:500 13px var(--font-mono);text-anchor:middle}
  .node .count{fill:var(--faint);font-size:10px;font-weight:400}
  .node:hover rect{fill:var(--raise)}
  .ov{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:var(--sp-3);
    margin-bottom:var(--sp-3)}
  .ov-tier{background:var(--card);border:1px solid var(--line);border-radius:var(--r-md);
    padding:var(--sp-3) var(--sp-4)}
  .ov-tier b{display:block;color:var(--strong);font-size:var(--fs-400)}
  .ov-tier span{color:var(--muted);font:400 var(--fs-200)/1.5 var(--font-mono)}
  .ov-links{display:flex;flex-wrap:wrap;gap:var(--sp-2);margin-bottom:var(--sp-5)}
  .ov-link{font:400 var(--fs-200) var(--font-mono);color:var(--muted);
    background:var(--card);border:1px solid var(--hairline);border-radius:999px;
    padding:3px 10px}
  .ov-link b{color:var(--accent-ink);font-weight:600}
  .legend{display:flex;flex-wrap:wrap;gap:var(--sp-3);margin-top:var(--sp-4)}
  .legend span{display:inline-flex;align-items:center;gap:6px;
    font:400 var(--fs-200) var(--font-mono);color:var(--muted)}
  .legend i{width:10px;height:10px;border-radius:3px;display:inline-block}
</style>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
@@NAV@@
<main id="main" class="page">
  <div class="section-h"><h2>Subsystem graph</h2><span class="rule"></span></div>
  <p class="lede">{count} subsystems and {edges} dependencies, read out of the source
  tree. Applications sit on platform backends, which sit on the portable protocol.</p>
  {overview}
  <div class="shell" id="shell">
    <div class="tools">
      <button type="button" data-z="out" aria-label="Zoom out">&#8722;</button>
      <button type="button" data-z="fit" aria-label="Fit the graph to the frame">Fit</button>
      <button type="button" data-z="in" aria-label="Zoom in">+</button>
      <button type="button" data-z="full" aria-label="Toggle full screen">&#10530;</button>
    </div>
    <div class="pan" id="pan">{svg}</div>
  </div>
  <div class="legend">{legend}</div>
  <p><small>Showing the {shown} dependencies of {edges} that carry five or more
  references; {hidden} lighter ones are hidden because a single include is real but
  is not structure. Drag to pan, &#8984;&#8202;scroll or pinch to zoom, &#10530; for full screen.
  Core only: modules, ports and applications. Tests, tooling and build glue are left out
  because they reference everything and hide the layering. Line weight is the number of
  references; hover a node or a line for detail. Built from <code>{commit}</code>.</small></p>
</main>
@@FOOTER@@
<script>
(function(){
  var shell=document.getElementById('shell'),pan=document.getElementById('pan'),
      svg=document.getElementById('g');
  var k=1,tx=0,ty=0;
  function apply(){pan.style.transform='translate('+tx+'px,'+ty+'px) scale('+k+')';}
  function fit(){
    var vb=svg.viewBox.baseVal, r=shell.getBoundingClientRect();
    k=Math.min(r.width/vb.width,(r.height||vb.height)/vb.height,1);
    tx=(r.width-vb.width*k)/2; ty=0; apply();
  }
  function size(){ shell.style.height=Math.min(svg.viewBox.baseVal.height,720)+'px'; }
  size(); fit();
  addEventListener('resize',function(){size();fit();});

  var down=false,px=0,py=0;
  shell.addEventListener('pointerdown',function(e){
    if(e.target.closest('.tools'))return;
    down=true;px=e.clientX;py=e.clientY;shell.classList.add('grabbing');
    shell.setPointerCapture(e.pointerId);
  });
  shell.addEventListener('pointermove',function(e){
    if(!down)return; tx+=e.clientX-px; ty+=e.clientY-py; px=e.clientX; py=e.clientY; apply();
  });
  shell.addEventListener('pointerup',function(e){
    down=false;shell.classList.remove('grabbing');
  });
  // Only zoom on the pinch/modifier gesture, so a plain wheel still scrolls
  // the page the way every other pane on the site does.
  shell.addEventListener('wheel',function(e){
    if(!e.ctrlKey&&!e.metaKey)return;
    e.preventDefault();
    var r=shell.getBoundingClientRect(),cx=e.clientX-r.left,cy=e.clientY-r.top,
        nk=Math.min(4,Math.max(.2,k*(e.deltaY<0?1.12:1/1.12)));
    tx=cx-(cx-tx)*(nk/k); ty=cy-(cy-ty)*(nk/k); k=nk; apply();
  },{passive:false});

  shell.querySelector('.tools').addEventListener('click',function(e){
    var b=e.target.closest('button'); if(!b)return;
    var a=b.dataset.z;
    if(a==='fit')fit();
    else if(a==='full'){shell.classList.toggle('full');size();fit();}
    else{var nk=Math.min(4,Math.max(.2,k*(a==='in'?1.2:1/1.2)));
         var r=shell.getBoundingClientRect(),cx=r.width/2,cy=r.height/2;
         tx=cx-(cx-tx)*(nk/k); ty=cy-(cy-ty)*(nk/k); k=nk; apply();}
  });
  addEventListener('keydown',function(e){
    if(e.key==='Escape'&&shell.classList.contains('full')){
      shell.classList.remove('full');size();fit();}
  });
})();
</script>
</body>
</html>
"""


def render(graph_json: Path) -> str:
    graph = json.loads(graph_json.read_text())
    sizes, all_edges = aggregate(graph)
    if not sizes:
        raise SystemExit("graph: no core subsystems in the graph data")
    edges = Counter({k: v for k, v in all_edges.items() if v >= MIN_EDGE})
    hidden = len(all_edges) - len(edges)

    clusters = sorted({n.split("/")[0] + "/" + label_of(n).split("_")[0]
                       for n in sizes})
    hue_of = {}
    for name in sorted(sizes):
        key = name.split("/")[0]
        hue_of[name] = HUES[TIERS.index(key) % len(HUES)] if key in TIERS else HUES[0]
    # One hue per tier reads as the layering; within a tier, position and the
    # label carry identity. Keeps to the system's "one accent per region" rule.
    for name in sorted(sizes):
        hue_of[name] = HUES[TIERS.index(name.split("/")[0])]

    body, _ = svg(sizes, edges, hue_of)
    legend = "".join(
        f'<span><i style="background:{HUES[i]}"></i>{TIER_CAPTION[t]}</span>'
        for i, t in enumerate(TIERS))

    page = PAGE
    for key, value in (("{count}", str(len(sizes))),
                       ("{edges}", str(len(all_edges))),
                       ("{hidden}", str(hidden)),
                       ("{commit}", esc(graph.get("built_at_commit", "unknown")[:12])),
                       ("{overview}", overview(sizes, edges)),
                       ("{legend}", legend),
                       ("{shown}", str(len(edges))),
                       ("{svg}", body)):
        page = page.replace(key, value)
    return page


if __name__ == "__main__":
    import sys
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "graphify-out/graph.json")
    sys.stdout.write(render(source))
