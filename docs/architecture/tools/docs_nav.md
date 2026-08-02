<!-- generated documentation — edit the source, not this file -->
# `tools/docs_nav.py`

Give the rendered site one curated reading order.

The generator ranks the guide list by keyword buckets, which is a reasonable
default and a poor journey: install and configure material was scattered, and
a reader finishing one page got no pointer to the next. This pass owns the
order in one place:

  * the landing page's Guides section is rebuilt into curated buckets
    (Set up first, deep dives after) — and because the sidebar shim mirrors
    the landing page's buckets, the sidebar follows automatically,
  * every page on the journey gets a prev/next pager, so there is always a
    next page and it is always the right one,
  * each guide's hero eyebrow names its bucket instead of the generic
    "Guide".

The buckets and the journey are the same list, so they cannot drift apart.
A guide added without a place in it fails the build here, on purpose: the
author decides where it belongs, or this pass would silently undo the point
of having a curated order.

Run from the repo root, after docs_start.py (start.html must exist to lead
the journey) and before docs_graph.py (whose sidebar shim reads the landing
page's buckets as rebuilt here).

```mermaid
flowchart TD
  add_pagers --> fail
  add_pagers --> page_title
```

## API

### `fail(msg: str) -> int`
`tools/docs_nav.py:95`

Print an error message to stderr prefixed with "docs_nav: " and return 1.

**called by** `add_pagers`, `curate_index`

### `page_title(slug: str) -> str`
`tools/docs_nav.py:101`

Extract the page title from the <title> tag in an HTML file by path slug; return the slug itself if no title is found.

**called by** `add_pagers`

### `curate_index(index: Path) -> int | None`
`tools/docs_nav.py:107`

Rebuild the Guides section into the journey's buckets.

**called by** `main`  ·  **calls** `fail`

### `add_pagers() -> int | None`
`tools/docs_nav.py:142`

Inject previous/next navigation cards before </main> on each journey page, updating the eyebrow label to the guide bucket name (except on the start page); do nothing if pager already present.

**called by** `main`  ·  **calls** `fail`, `page_title`

### `main() -> int`
`tools/docs_nav.py:187`

Check that the rendered site exists, rebuild the landing-page guides into journey buckets, inject previous/next pagers into journey pages, report results.

**calls** `add_pagers`, `curate_index`
