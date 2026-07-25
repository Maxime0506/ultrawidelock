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
`tools/docs_github.py:93`

owner/repo for the origin remote, or '' if none.

**called by** `main`

<details><summary>Undocumented (5)</summary>

- `topbar_chip`
- `tail_block`
- `hero_button`
- `quickstart`
- `main`

</details>
