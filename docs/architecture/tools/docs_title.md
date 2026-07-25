<!-- generated documentation — edit the source, not this file -->
# `tools/docs_title.py`

Title the generated pages after the repository, not after the checkout directory.

Older page-generator releases took the project name from the basename of the
directory they ran in. In a linked worktree that name is the worktree
directory's, not the repository's, which put the wrong title on every page and
in the committed docs/ tree. The current release derives the name from git
itself, so this pass is a safety net that normally rewrites nothing.

The net only looks where a title can actually sit: the <title> tag, the
sidebar brand, and h1 headings in the rendered pages; the markdown H1 lines in
the two committed docs/ pages. It must not look anywhere else. A checkout can
be named after an ordinary word of the prose — a worktree named after, say,
the very thing this site is — and a blanket replacement would then rename that
word through running text, the generator's own ownership stamp (breaking its
regeneration), and the reference tree's rendered source listings. The
reference tree is excluded entirely: doxygen takes its project name from
docs/Doxyfile, never from the checkout.

The repository name comes from the common git directory, which every worktree
shares, so it is the same value from any checkout. When the two names agree
this is a no-op, which is the case in the main checkout.

Run from the repo root, after the generators and before the link pass.

## API

### `git(*args: str) -> str`
`tools/docs_title.py:43`

Run a git command and return its stdout stripped, or an empty string if git is not found or the command fails.

**called by** `main`

### `retitle(raw: bytes, slots, token: re.Pattern[bytes], repo: bytes) -> tuple[bytes, int]`
`tools/docs_title.py:53`

Substitute the checkout name inside title slots only; count edits.

**called by** `main`

### `fix(m: re.Match[bytes]) -> bytes`
`tools/docs_title.py:57`

Replace a single occurrence of the checkout name with the repo name inside a matched title slot; increment the edit counter and return the fixed bytes.

### `main() -> int`
`tools/docs_title.py:69`

Detect worktree name mismatch and retitle all docstring title slots and HTML title tags from checkout name to repo name (one-time per worktree); report count of edits and files touched.

**calls** `git`, `retitle`
