#!/usr/bin/env python3
"""Build the flyable 3D graph of the code surface: dist/graph/index.html.

This is v0.3.0's graph3d page, restored. The renderer, the camera arrival, the
cluster and containment forces, the parallax dust and the detail panel are that
page's; what changed is where the data comes from. The original mined
documate's per-file markdown and its search index, and both are gone, so the
same payload is built from graphify's AST graph instead:

    graphify . --update --code-only

Node and link shape is unchanged, so the template is untouched apart from its
palette and its links:

    nodes  {id, name, grp, slug, blurb}
    links  {source, target}
    slots  group -> colour slot        colors  the slot palette

Blurbs come from each file's own header comment, which is better than what the
markdown carried: it is the text the author wrote next to the code.

3d-force-graph (MIT) is vendored under web/vendor/, which is gitignored. With
no vendor copy the page is skipped and the site builds without it, the same
way the twin is skipped without emscripten.
"""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

CORE_TOP = ("modules", "ports", "apps")
VENDORED = ("dwt_uwb_driver", "detools")
LIB = "3d-force-graph.min.js"
REPO = "https://github.com/ultrawidelock/ultrawidelock/blob/main/"

# Slot palette. The design system's syntax colours, which are a categorical
# spread already tuned for a dark surface -- and the stage here is always dark,
# so one set serves. Every node also carries its name and its group, so colour
# is never the only channel.
# Cool only. The earlier set borrowed the syntax palette wholesale, which
# dragged an orange and a warm yellow onto a mint-on-teal stage and read as
# the old warm-paper theme. These are the theme's own mints, teals, blues and
# violets; every node still carries its name and group as text, so colour
# stays a second channel rather than the only one.
COLORS = ["#2ee6b8", "#7fb8ff", "#8fd6a8", "#d5a8f0", "#5fecc4",
          "#a9d6ff", "#41c98a", "#63e0f0", "#b8e6c8", "#9fd0ff",
          "#8ff5d8", "#c8b3f0", "#7ff0d4", "#6fd7c0"]

# A file's header comment. Sources here open with an SPDX line, then the real
# description in a // run or a /** @file */ block, so the FIRST comment is
# almost never the one wanted: taking it yielded 17 blurbs out of 393 and every
# one of them was licence boilerplate. Collect every candidate, drop the ones
# that are only a licence, and take the first that says something.
COMMENT = re.compile(r"/\*[*!]?(.*?)\*/|((?:^[ \t]*//[^\n]*\n)+)", re.S | re.M)
LICENCE = re.compile(
    r"SPDX-License-Identifier|Copyright|Public Domain|Licensed under|"
    r"CC0|Apache License|WITHOUT WARRANTIES", re.I)
NOISE = re.compile(r"@file\s+\S+\s*[-—–]?\s*|@brief\s*")


def blurb_for(path: Path) -> str:
    """One sentence from the file's header comment, or an empty string."""
    try:
        head = path.read_text(errors="replace")[:3000]
    except OSError:
        return ""

    for match in COMMENT.finditer(head):
        raw = match.group(1) or match.group(2) or ""
        text = re.sub(r"^\s*(?://|\*)\s?", "", raw, flags=re.M)
        text = NOISE.sub("", text)
        text = " ".join(re.sub(r"[`*]", "", text).split())
        if len(text) < 24 or LICENCE.search(text):
            continue
        # First sentence: the panel gives this one line.
        out = re.split(r"(?<=[.!?])\s+", text)[0]
        return (out[:240].rstrip() + "…") if len(out) > 240 else out
    return ""


def keep(source_file: str) -> bool:
    if not source_file or source_file.split("/")[0] not in CORE_TOP:
        return False
    return not any(v in source_file.split("/") for v in VENDORED)


def group_of(source_file: str) -> str:
    parts = source_file.split("/")
    return "/".join(parts[:2]) if len(parts) > 1 else parts[0]


def build(graph_json: Path, root: Path) -> dict:
    graph = json.loads(graph_json.read_text())
    owner = {n["id"]: n.get("source_file", "") for n in graph["nodes"]}

    files = sorted({sf for sf in owner.values() if keep(sf)})
    nodes = [{
        "id": sf,
        "name": sf.rsplit("/", 1)[-1],
        "grp": group_of(sf),
        "slug": REPO + sf,
        "blurb": blurb_for(root / sf),
    } for sf in files]

    weight: Counter = Counter()
    for link in graph["links"]:
        a, b = owner.get(link["source"], ""), owner.get(link["target"], "")
        if keep(a) and keep(b) and a != b:
            weight[(a, b) if a < b else (b, a)] += 1
    links = [{"source": a, "target": b} for (a, b) in sorted(weight)]

    # Slots in sorted group order, so a rebuild does not reshuffle the colours.
    groups = sorted({n["grp"] for n in nodes})
    slots = {g: i for i, g in enumerate(groups)}
    return {"nodes": nodes, "links": links, "syms": {},
            "slots": slots, "colors": COLORS}


def render(graph_json: Path, root: Path) -> str:
    data = build(graph_json, root)
    tpl = Path(__file__).with_name("graph3d.tpl.html").read_text()
    return (tpl.replace("@@DATA@@", json.dumps(data, separators=(",", ":")))
               .replace("@@LIB@@", LIB))


if __name__ == "__main__":
    import sys
    here = Path(__file__).resolve().parents[2]
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "graphify-out/graph.json"
    sys.stdout.write(render(src, here))
