<!-- generated documentation — edit the source, not this file -->
# `tools/docs_cmds.py`

Render runnable command blocks as one copy chip per command.

A guide's bash block renders as a plain <pre>: the trailing `# comment` sits
in the same monospace run as the command, and the block-level copy button
copies comments and all. For a block of commands the reader wants the
opposite: each command on its own row, the comment visibly muted, and a Copy
button that yields exactly the command — the chip treatment the landing
page's quick start already uses. The chip CSS and the .js-copycmd handler
ship on every page, so the rewrite is markup only.

Only blocks that are unambiguously command sequences are touched: every
non-blank line must start with an allowlisted command (optionally prefixed
with VAR=value assignments). Device logs, pseudocode and C fragments never
match and render as before.

Run from the repo root, after docs_nav.py and before the link pass.

## API

### `chip(cmd: str) -> str`
`tools/docs_cmds.py:39`

Render a shell command as a copyable chip: dollar sign, escaped command, and copy button with data attribute.

**called by** `rewrite`

### `rewrite(match: re.Match[str], counter: list[int]) -> str`
`tools/docs_cmds.py:51`

Rewrite a code block to copy chips: extract shell commands (one per line, optional trailing comment stripped), verify format, render each as a chip. Returns the substituted HTML or original text if no commands match. Increments counter on success.

**called by** `main`  ·  **calls** `chip`

### `main() -> int`
`tools/docs_cmds.py:65`

Rewrite command blocks to copy chips on all rendered site pages: find fenced code blocks matching the allowlist, call rewrite for each, inject CSS once per page. Print counts of blocks and pages modified.

**calls** `rewrite`
