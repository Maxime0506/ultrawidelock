<!-- generated documentation — edit the source, not this file -->
# `tools/docs_start.py`

Give the rendered site a real "Get started" landing.

The hero's Get-started button used to deep-link straight into the ESP32
bring-up checklist — a fine first page for exactly one kind of reader.
This pass builds start.html instead, ordered by what the reader is willing
to spend rather than by subsystem: three routes of escalating commitment —
the digital twin (nothing to install), the browser flasher plus Apple Home
commissioning (no toolchain), then the full clone-bootstrap-flash setup —
followed by the reference tracks for a reader who is already running. Each
is a card that drills down in place to the commands, installs and guides
that route needs. The page is assembled from an existing rendered guide
page, so it always carries the current shell — sidebar, palette, theme
toggle and the other passes' injections.

Route 2's browser-flasher row is not written here: docs_flash.py injects it
into `#flash-slot` only when a firmware image was actually staged, so a
checkout with no release never shows an Install link that 404s.

Also part of wayfinding, on every page:

  * the sidebar gains a Get-started entry next to Overview,
  * the search button gets the visual weight a primary control deserves
    (accent tint, a couple of attention pings on load) and the palette a
    springier open — search is how readers actually move around, so it
    should not look like chrome.

Run from the repo root, after docs_github.py and before docs_graph.py, so
the page exists before the sitewide shims and the link pass run.

## API

### `repo_url() -> str`
`tools/docs_start.py:49`

Return the GitHub URL (https://github.com/owner/repo) of the origin remote, or an empty string if the remote is not configured or not a GitHub URL.

**called by** `main`

### `chip(cmd: str) -> str`
`tools/docs_start.py:62`

Render a shell command in a copyable chip with a Copy button and dollar-sign prompt. No comments allowed inline; context goes in surrounding prose.

**called by** `deeper_html`, `routes_html`

### `row(href: str, name: str, desc: str) -> str`
`tools/docs_start.py:73`

Render a navigation card row with a link, title, and description text.

**called by** `deeper_html`, `routes_html`

### `routes_html(gh: str) -> list[tuple[str, str, str, str, str]]`
`tools/docs_start.py:200`

Return the three getting-started routes as (icon, cost, title, subtitle, body) tuples, ordered by what each one asks of the reader: a browser tab, a board and no toolchain, then a full checkout. Embeds the provided GitHub URL into clone and release rows.

**called by** `main_html`  ·  **calls** `chip`, `row`

### `deeper_html(gh: str) -> list[tuple[str, str, str, str, str]]`
`tools/docs_start.py:353`

Return the reference tracks shown under the three routes as (icon, cost, title, subtitle, body) tuples, for a reader who is already running. The cost field is empty: these are not a fourth route. Embeds the provided GitHub URL into the repository rows.

**called by** `main_html`  ·  **calls** `chip`, `row`

### `card(ico: str, when: str, title: str, sub: str, body: str) -> str`
`tools/docs_start.py:418`

Render one route or track as a collapsible summary card. An empty cost field omits the badge, which is what separates a reference track from a numbered route.

**called by** `cards`

### `cards(items: list[tuple[str, str, str, str, str]]) -> str`
`tools/docs_start.py:429`

Render a list of route or track tuples as one run of cards.

**called by** `main_html`  ·  **calls** `card`

### `main_html(gh: str) -> str`
`tools/docs_start.py:434`

Render the main content section of the Get-Started landing page: the three escalating-commitment routes, then the reference tracks for a reader who is already running. Embeds the provided GitHub URL into clone, release and repository rows. Returns HTML.

**called by** `build_page`  ·  **calls** `cards`, `deeper_html`, `routes_html`

### `build_page(template: str, gh: str) -> str`
`tools/docs_start.py:472`

Build the Get-Started page by injecting main_html(gh) into the site template, then update the hero button link and search index. Applies title, og:title, breadcrumb, and active-nav-marker rewrites. Returns the modified page as a string.

**called by** `main`  ·  **calls** `main_html`

### `add_search_row(page_name: str) -> None`
`tools/docs_start.py:501`

Add an entry for the given page name to the nav.js search array if not already present. Inserts at position 1 (after the index entry) and rewrites the JSON in place. Does nothing silently if nav.js is absent.

**called by** `main`

### `main() -> int`
`tools/docs_start.py:519`

Render the Get-Started landing page and inject it into site/start.html, wire the wayfinding guide into every page, and update navigation and hero-button references. Reports counts of pages modified and returns 1 if the template layout has changed. Requires the site to be rendered first.

**calls** `add_search_row`, `build_page`, `repo_url`
