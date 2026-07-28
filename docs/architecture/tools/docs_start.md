<!-- generated documentation — edit the source, not this file -->
# `tools/docs_start.py`

Give the rendered site a real "Get started" landing.

The hero's Get-started button used to deep-link straight into the ESP32
bring-up checklist — an fine first page for exactly one kind of reader.
This pass builds start.html instead: one landing that holds every track
(hardware, toolchain, build and test, firmware internals, protocol
research, project and CI), each a card that drills down in place to the
commands, installs and guides that track needs. The page is assembled from
an existing rendered guide page, so it always carries the current shell —
sidebar, palette, theme toggle and the other passes' injections.

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
`tools/docs_start.py:42`

Return the GitHub URL (https://github.com/owner/repo) of the origin remote, or an empty string if the remote is not configured or not a GitHub URL.

**called by** `main`

### `chip(cmd: str) -> str`
`tools/docs_start.py:55`

Render a shell command in a copyable chip with a Copy button and dollar-sign prompt. No comments allowed inline; context goes in surrounding prose.

**called by** `main_html`

### `row(href: str, name: str, desc: str) -> str`
`tools/docs_start.py:66`

Render a navigation card row with a link, title, and description text.

**called by** `main_html`

### `main_html(gh: str) -> str`
`tools/docs_start.py:129`

Render the main content section of the Get-Started landing page as a series of collapsible track cards. Each card contains links, code chips, and prose explaining the Hardware, Software, Build/Test/Verify, Architecture, Protocol, and Project tracks. Embeds the provided GitHub URL into clone and repository-link rows. Returns HTML.

**called by** `build_page`  ·  **calls** `chip`, `row`

### `build_page(template: str, gh: str) -> str`
`tools/docs_start.py:288`

Build the Get-Started page by injecting main_html(gh) into the site template, then update the hero button link and search index. Applies title, og:title, breadcrumb, and active-nav-marker rewrites. Returns the modified page as a string.

**called by** `main`  ·  **calls** `main_html`

### `add_search_row(page_name: str) -> None`
`tools/docs_start.py:317`

Add an entry for the given page name to the nav.js search array if not already present. Inserts at position 1 (after the index entry) and rewrites the JSON in place. Does nothing silently if nav.js is absent.

**called by** `main`

### `main() -> int`
`tools/docs_start.py:335`

Render the Get-Started landing page and inject it into site/start.html, wire the wayfinding guide into every page, and update navigation and hero-button references. Reports counts of pages modified and returns 1 if the template layout has changed. Requires the site to be rendered first.

**calls** `add_search_row`, `build_page`, `repo_url`
