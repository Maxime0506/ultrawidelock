<!-- generated documentation — edit the source, not this file -->
# `tools/docs_github.py`

Point the rendered site back at its GitHub repository.

The page generator renders prose, navigation and reference pages; it does not
know where the source lives. This pass adds that context, with the repository
URL derived from the origin remote at build time, never hardcoded:

  * every top-level page gets a repository chip in the top bar: the GitHub
    mark, the owner/repo name, and live star/fork counts fetched client-side
    from the GitHub API (cached in localStorage for an hour; the counts stay
    hidden if the API is unreachable, the link still works).
  * the landing page's hero gets a GitHub button next to the existing calls
    to action, and a "Get running" section under the demo figure: clone to
    flashed board in five copyable steps, mirroring the README quickstart.

Idempotent for the same reason docs_media.py is: when the page generator is
not configured, the earlier passes run over a site/ kept from a previous
build, so a page may already carry the injections. Run from the repo root,
after docs_media.py and before the link pass.

## API

### `repo_slug() -> str`
`tools/docs_github.py:99`

owner/repo for the origin remote, or '' if none.

**called by** `main`

### `topbar_chip(slug: str, url: str) -> bytes`
`tools/docs_github.py:116`

Render a GitHub repository link chip for the top navigation bar, showing the repo slug with live star and fork counts populated by JavaScript. The chip targets the provided GitHub URL. Returns encoded HTML.

**called by** `main`

### `tail_block() -> bytes`
`tools/docs_github.py:129`

Render inline CSS and JavaScript for the GitHub repository chip and quickstart section. The script fetches live star and fork counts from the GitHub API and caches them in localStorage for one hour. Returns encoded HTML.

**called by** `main`

### `hero_button(url: str) -> bytes`
`tools/docs_github.py:171`

Render a GitHub link button with 15px SVG icon and "GitHub" text.

**called by** `main`

### `quickstart(url: str) -> bytes`
`tools/docs_github.py:179`

Render a numbered quickstart checklist with shell commands, formatted for injection into the Get-Running section of the landing page. Each step embeds a command in a copyable chip with the provided URL template applied. Returns encoded HTML.

**called by** `main`

### `main() -> int`
`tools/docs_github.py:209`

Inject GitHub repository metadata into the rendered site: a repository chip on every page (anchored to the theme toggle), a GitHub button on the landing page hero section, and a quickstart checklist below the features list. Reports counts of pages modified and returns 0 on success or 1 if layout anchors are missing. Requires the site to be already rendered and origin remote configured.

**calls** `hero_button`, `quickstart`, `repo_slug`, `tail_block`, `topbar_chip`
