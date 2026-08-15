#!/usr/bin/env python3
"""Render the subsystem graph as a standalone SVG page.

Input is graphify-out/graph.json, which graphify builds from the source tree:

    graphify . --update --code-only

That directory is gitignored and the tool is not in this repository, so the
graph is an enrichment: when the file is absent the site build skips this page
and says so, exactly as it does when emscripten is missing.

What gets drawn is the CORE only -- modules, ports and applications. Tests,
tooling, scripts and build glue are dropped: tests in particular reference
every module, so including them flattens the layering into a hairball and
hides the one thing this graph exists to show. The vendored Qorvo driver and
detools trees are dropped for the same reason they are exempt from the brand
gate: we do not author them.

File-level nodes are rolled up to their subsystem and edges are aggregated with
a weight, so 7,782 nodes and 17,830 links become something a person can read.
"""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

CORE_TOP = ("modules", "ports", "apps")
VENDORED = ("dwt_uwb_driver", "detools")

# Tier order, top to bottom: the product, what carries it, what it is.
TIERS = ["apps", "ports", "modules"]
TIER_CAPTION = {
    "apps": "applications",
    "ports": "platform backends",
    "modules": "portable protocol",
}

W, H = 1180, 620
PAD_X, TIER_H = 70, 190
NODE_H = 44


def subsystem(path: str) -> str | None:
    """Roll a source path up to the subsystem that owns it."""
    if not path:
        return None
    parts = path.split("/")
    if len(parts) < 2 or parts[0] not in CORE_TOP:
        return None
    if any(v in parts for v in VENDORED):
        return None
    if parts[1].endswith(".md"):          # README nodes are not subsystems
        return None
    return f"{parts[0]}/{parts[1]}"


def aggregate(graph: dict) -> tuple[Counter, Counter]:
    """File-level graph -> subsystem sizes and weighted subsystem edges."""
    sub_of: dict[str, str] = {}
    for node in graph["nodes"]:
        name = subsystem(node.get("source_file", ""))
        if name:
            sub_of[node["id"]] = name

    sizes: Counter = Counter(sub_of.values())
    edges: Counter = Counter()
    for link in graph["links"]:
        a, b = sub_of.get(link["source"]), sub_of.get(link["target"])
        if a and b and a != b:
            edges[(a, b) if a < b else (b, a)] += 1
    return sizes, edges


def layout(sizes: Counter, edges: Counter) -> dict[str, tuple[float, float, float]]:
    """Deterministic placement, so a diff of the page is a diff of the architecture.

    Within a tier the busiest subsystems sit centre, which keeps their many
    edges short and the crossings down.
    """
    degree: Counter = Counter()
    for (a, b), weight in edges.items():
        degree[a] += weight
        degree[b] += weight

    placed: dict[str, tuple[float, float, float]] = {}
    for row, tier in enumerate(TIERS):
        members = sorted((n for n in sizes if n.startswith(tier + "/")),
                         key=lambda n: (-degree[n], n))
        ordered: list[str] = []
        for i, name in enumerate(members):
            ordered.insert(0, name) if i % 2 else ordered.append(name)
        if not ordered:
            continue
        y = 80 + row * TIER_H
        span = W - 2 * PAD_X
        for i, name in enumerate(ordered):
            x = PAD_X + span * (i + 0.5) / len(ordered)
            label = name.split("/")[1].replace("ultrawidelock_", "")
            placed[name] = (x, y, max(96.0, 30 + len(label) * 8.2))
    return placed


def esc(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg(sizes: Counter, edges: Counter) -> str:
    pos = layout(sizes, edges)
    heaviest = max(edges.values()) if edges else 1
    # xmlns is not required inline in HTML, but it makes the fragment valid on
    # its own, so the same markup can be saved out as a .svg and still render.
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
           f'width="100%" role="img" aria-label="Subsystem dependency graph: '
           f'applications over platform backends over the portable protocol">']

    for row, tier in enumerate(TIERS):
        y = 80 + row * TIER_H
        out.append(f'<text class="g-tier" x="8" y="{y - 46}">{TIER_CAPTION[tier]}</text>')
        out.append(f'<line class="g-rule" x1="8" y1="{y - 38}" x2="{W - 8}" y2="{y - 38}"/>')

    for (a, b), weight in sorted(edges.items(), key=lambda kv: kv[1]):
        if a not in pos or b not in pos:
            continue
        (x1, y1, _), (x2, y2, _) = pos[a], pos[b]
        if y1 == y2:                              # same tier: arc clear of the row
            lift = y1 - 56 if y1 > 100 else y1 + 56
            path = f"M{x1:.1f},{y1} Q{(x1 + x2) / 2:.1f},{lift} {x2:.1f},{y2}"
        else:
            half = (y1 + y2) / 2
            path = (f"M{x1:.1f},{y1} C{x1:.1f},{half} "
                    f"{x2:.1f},{half} {x2:.1f},{y2}")
        share = weight / heaviest
        out.append(f'<path class="g-edge" d="{path}" stroke-width="{0.7 + 3.6 * share:.2f}"'
                   f' opacity="{0.16 + 0.5 * share:.2f}"><title>{esc(a)} &#8596; '
                   f'{esc(b)}: {weight} references</title></path>')

    for name, (x, y, width) in pos.items():
        label = name.split("/")[1].replace("ultrawidelock_", "")
        out.append(
            f'<g class="g-node g-{name.split("/")[0]}">'
            f'<rect x="{x - width / 2:.1f}" y="{y - NODE_H / 2}" width="{width:.1f}" '
            f'height="{NODE_H}" rx="7"/>'
            f'<text x="{x:.1f}" y="{y - 1:.1f}">{esc(label)}</text>'
            f'<text class="g-count" x="{x:.1f}" y="{y + 14:.1f}">{sizes[name]}</text>'
            f'<title>{esc(name)}: {sizes[name]} nodes</title></g>')

    out.append("</svg>")
    return "\n".join(out)


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
  .g-wrap{background:var(--card);border:1px solid var(--line);border-radius:var(--r-lg);
    padding:var(--sp-4);overflow-x:auto}
  .g-tier{fill:var(--muted);font:500 11px var(--font-mono);letter-spacing:.09em;
    text-transform:uppercase}
  .g-rule{stroke:var(--line);stroke-width:1}
  .g-edge{fill:none;stroke:var(--accent)}
  .g-node rect{fill:var(--surface);stroke:var(--line);stroke-width:1}
  .g-node text{fill:var(--strong);font:500 12.5px var(--font-mono);text-anchor:middle}
  .g-node .g-count{fill:var(--muted);font-size:10px;font-weight:400}
  .g-node:hover rect{stroke:var(--accent)}
</style>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="topbar">
  <a class="wordmark" href="../index.html">UltraWideLock</a>
  <div class="spacer"></div>
  <nav class="navlinks">
    <a class="navlink" href="../twin/index.html">Twin</a>
    <a class="navlink" href="../flash/index.html">Flash</a>
    <a class="navlink" href="https://github.com/ultrawidelock/ultrawidelock">Source</a>
  </nav>
  <button class="icon-btn" data-theme-toggle aria-pressed="false" title="Switch theme"><svg class="sun" viewBox="0 0 16 16" aria-hidden="true" fill="currentColor"><circle cx="8" cy="8" r="4"/><circle cx="8" cy="1.4" r="1.1"/><circle cx="8" cy="14.6" r="1.1"/><circle cx="1.4" cy="8" r="1.1"/><circle cx="14.6" cy="8" r="1.1"/></svg><svg class="moon" viewBox="0 0 16 16" aria-hidden="true" fill="currentColor"><path d="M8 1a7 7 0 1 0 7 7 5.5 5.5 0 0 1-7-7Z"/></svg></button>
</header>
<main id="main" class="page">
  <div class="section-h"><h2>Subsystem graph</h2><span class="rule"></span></div>
  <p class="lede">{count} subsystems and {edges} dependencies, read out of the source tree.
  Applications sit on platform backends, which sit on the portable protocol.</p>
  <div class="g-wrap">{svg}</div>
  <p><small>Core only: modules, ports and applications. Tests, tooling and build glue are
  left out because they reference everything and hide the layering. Line weight is the
  number of references; hover a node or a line for detail. Built from
  <code>{commit}</code>.</small></p>
</main>
<script src="../assets/design/theme-toggle.js"></script>
</body>
</html>
"""


def render(graph_json: Path) -> str:
    graph = json.loads(graph_json.read_text())
    sizes, edges = aggregate(graph)
    if not sizes:
        raise SystemExit("graph: no core subsystems in the graph data")
    # Plain replacement, not .format(): the page carries CSS and JS braces.
    page = PAGE
    for key, value in (("{count}", str(len(sizes))),
                       ("{edges}", str(len(edges))),
                       ("{commit}", esc(graph.get("built_at_commit", "unknown")[:12])),
                       ("{svg}", svg(sizes, edges))):
        page = page.replace(key, value)
    return page


if __name__ == "__main__":
    import sys
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "graphify-out/graph.json")
    sys.stdout.write(render(source))
