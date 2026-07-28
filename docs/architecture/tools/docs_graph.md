<!-- generated documentation — edit the source, not this file -->
# `tools/docs_graph.py`

Make the architecture page's dependency graph legible.

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

## API

### `parse_edges(mermaid: str) -> tuple[str, list[tuple[str, str]]]`
`tools/docs_graph.py:200`

Parse a mermaid graph block into its header line and a list of (source, target) node pairs, dropping self-loops.

**called by** `figures`

### `stem_dirs(page: str) -> dict[str, str]`
`tools/docs_graph.py:211`

module name -> its source directory, from the page's file headings.

A stem can exist both in modules/ and in a port's copy; the shared core
is the one the import graph describes, so modules/ wins.

**called by** `figures`

### `cluster_of(directory: str) -> str`
`tools/docs_graph.py:228`

Return the top-level cluster name for a source directory: woz_uwb submodule names become "woz_uwb/submodule", others return their first path component.

**called by** `cluster_key`, `figures`, `group_sections`, `head`

### `color_css(names: list[str], clusters: dict[str, set[str]]) -> str`
`tools/docs_graph.py:235`

Theme-aware cluster colors: tinted node fills, hue strokes, dot vars.

Selectors go by mermaid's DOM ids (flowchart-<node>-<n>), with !important
to beat the svg's own id-prefixed stylesheet; the theme toggle scope must
win over the OS preference in both directions.

**called by** `figures`

### `figures(page: str, mermaid: str) -> tuple[str, dict[str, int]]`
`tools/docs_graph.py:289`

Build the architecture overview and clustered detail graphs from a mermaid definition, returning the HTML block and a cluster-to-color-slot mapping.

**called by** `main`  ·  **calls** `cluster_key`, `cluster_of`, `color_css`, `parse_edges`, `stem_dirs`

### `cluster_key(n: str) -> str | None`
`tools/docs_graph.py:307`

Map a node name to its cluster, or None if the node has no directory heading on the page.

**called by** `figures`  ·  **calls** `cluster_of`

### `chip(m: re.Match) -> str`
`tools/docs_graph.py:374`

Format a single chip reference as an HTML link with code formatting, extracting the basename from the full path.

### `chips_block(m: re.Match) -> str`
`tools/docs_graph.py:383`

Rewrite a block of chip references by applying link and formatting to each one, preserving leading and trailing whitespace.

### `tidy_page(page: str) -> str`
`tools/docs_graph.py:388`

The same de-noising the architecture sections get, on a module page:
base-name chips with the full path on hover, and no "@file <name> — "
prefix repeating the file name the hero already shows.

**called by** `main`

### `tidy_sections(page: str, slots: dict[str, int]) -> tuple[str, int, int]`
`tools/docs_graph.py:399`

Short file-name headings with a directory eyebrow; base-name chips.

Directories that form a cluster in the graph get its color dot in the
eyebrow, correlating each section with the graphs above.

**called by** `main`

### `head(m: re.Match) -> str`
`tools/docs_graph.py:406`

Rewrite a module section heading with a shortened directory name, optional color-slot dot, and a hyperlinked source filename.

**calls** `cluster_of`

### `group_sections(page: str, slots: dict[str, int]) -> tuple[str, int]`
`tools/docs_graph.py:459`

Fold the flat run of per-module sections into per-cluster drill-downs.

One collapsed group per directory cluster, in the graphs' color order,
each module a compact link row (name + clamped blurb). Dependency rows
are dropped here — the graph above carries them, and every module page
keeps its own. The group holding the graphs' entry point opens by
default.

**called by** `main`  ·  **calls** `cluster_of`

### `eat(m: re.Match) -> str`
`tools/docs_graph.py:470`

Extract a section heading metadata triple and blurb into the accumulator, returning empty string to erase the matched text.

### `side_shim(slots: dict[str, int], buckets: list[tuple[str, list[str]]]) -> str`
`tools/docs_graph.py:571`

Generate the sidebar shim HTML with color-slot CSS variables and JSON-encoded cluster/bucket metadata for the interactive architecture sidebar.

**called by** `main`

### `main() -> int`
`tools/docs_graph.py:583`

Restructure the rendered architecture page: split its graph into overview and detail, shorten module headings, compact dependency rows, group sections by cluster, and apply layout shimming and sidebar injection to all HTML files.

**calls** `figures`, `group_sections`, `side_shim`, `tidy_page`, `tidy_sections`
