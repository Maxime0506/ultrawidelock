#!/usr/bin/env python3
"""A small Markdown renderer, sized to the guides in docs/.

Not a general implementation and not trying to be. A survey of the 18 guides
found exactly twelve constructs in use, and this handles those twelve:

    2213 paragraphs   1394 code spans   394 bold      385 table rows
     252 headings      206 bullets       91 links      64 fenced blocks
      42 italics        33 numbered      30 quotes      1 html block

No images appear in any guide, so no image syntax. Anything outside that set
renders as literal text rather than silently disappearing.

The alternative was `pip install markdown`, which would have broken the one
property this build has that the pipeline it replaced did not: a fresh clone
builds the whole site with stdlib Python and nothing installed.
"""

from __future__ import annotations

import html
import re

INLINE_CODE = re.compile(r"`([^`\n]+)`")
# Non-greedy and permitting a single asterisk inside: the guides nest
# italics in bold ("**the *plaintext* length**"), which a [^*] class
# cannot span. BOLD runs before ITALIC so the inner pair still resolves.
BOLD = re.compile(r"\*\*([^\n]+?)\*\*")
ITALIC = re.compile(r"(?<![*\w])\*([^*\n]+)\*(?!\*)")
LINK = re.compile(r"\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*)$")
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
BULLET = re.compile(r"^(\s*)[-*+]\s+(.*)$")
NUMBER = re.compile(r"^(\s*)\d+\.\s+(.*)$")
TABLE_SEP = re.compile(r"^\|[\s:|-]+\|?$")


def slug(text: str) -> str:
    """Stable heading anchor: lowercase, words joined by hyphens."""
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "section"


def inline(text: str) -> str:
    """Escape first, then re-introduce only the markup we support.

    Code spans are lifted out before anything else runs: `*` inside a code
    span is a literal asterisk, not emphasis, and the guides are full of
    things like `**argv` and `-Wl,*`.
    """
    spans: list[str] = []

    def stash(m: re.Match) -> str:
        spans.append(html.escape(m.group(1)))
        return f"\x00{len(spans) - 1}\x00"

    text = INLINE_CODE.sub(stash, text)
    text = html.escape(text)
    text = LINK.sub(
        lambda m: f'<a href="{_href(m.group(2))}"'
                  f'{_link_attrs(m.group(2))}>{m.group(1)}</a>', text)
    text = BOLD.sub(r"<strong>\1</strong>", text)
    text = ITALIC.sub(r"<em>\1</em>", text)
    return re.sub(r"\x00(\d+)\x00",
                  lambda m: f"<code>{spans[int(m.group(1))]}</code>", text)


REPO_BLOB = "https://github.com/ultrawidelock/ultrawidelock/blob/main/"


def _link_attrs(target: str) -> str:
    """Mark links that leave the site.

    The guides link outward constantly, and _href() sends anything with a path
    separator to the source browser, so most links here are off-site whether
    they were written that way or not. rel="noopener" because a new tab given
    window.opener can navigate the one it came from.
    """
    leaves = (target.startswith(("http://", "https://"))
              or ("/" in target and not target.startswith("#")))
    return ' data-ext rel="noopener"' if leaves else ""


def _href(target: str) -> str:
    """Sibling guides link to their rendered page; everything else to GitHub.

    These guides link outward constantly -- to overlays, headers, app READMEs,
    SECURITY.md. Those files are not part of the site, so rewriting them as
    site-relative produced 31 dead links. A guide is a sibling only when the
    target is a bare name: anything with a path separator belongs to the
    repository and is sent to the source browser instead.
    """
    if target.startswith(("http://", "https://", "#", "mailto:")):
        return html.escape(target, quote=True)
    clean = target.lstrip("./")
    while clean.startswith("../"):
        clean = clean[3:]
    if "/" not in target and target.endswith(".md"):
        return html.escape(target[:-3] + ".html", quote=True)
    if "/" not in target and not target.endswith(".md"):
        return html.escape(target, quote=True)
    return html.escape(REPO_BLOB + clean, quote=True)


def render(text: str) -> tuple[str, list[tuple[int, str, str]]]:
    """Markdown -> (html, [(level, title, anchor), ...]) for the TOC."""
    out: list[str] = []
    toc: list[tuple[int, str, str]] = []
    lines = text.splitlines()
    i, n = 0, len(lines)
    seen: set[str] = set()

    while i < n:
        line = lines[i]

        fence = FENCE.match(line)
        if fence:
            # Indented fences close on an indented fence: matching only at
            # column 0 left the block open and leaked its contents as prose.
            indent = len(line) - len(line.lstrip())
            lang, body, i = fence.group(1), [], i + 1
            while i < n and not lines[i].lstrip().startswith("```"):
                body.append(lines[i][indent:] if lines[i][:indent].isspace()
                            or not lines[i][:indent] else lines[i])
                i += 1
            i += 1
            cap = (f'<div class="cap"><span>{html.escape(lang)}</span></div>'
                   if lang else "")
            out.append(f'<div class="prewrap">{cap}<pre><code>'
                       f'{html.escape(chr(10).join(body))}</code></pre></div>')
            continue

        head = HEADING.match(line)
        if head:
            level, title = len(head.group(1)), head.group(2).strip()
            anchor = slug(title)
            while anchor in seen:                       # keep anchors unique
                anchor += "-x"
            seen.add(anchor)
            toc.append((level, title, anchor))
            # A heading without a way to link to it is a heading nobody can
            # send anyone to. The anchor is hidden until the heading is
            # hovered or the link itself is focused, so it costs no ink.
            link = (f'<a class="anchor" href="#{anchor}" '
                    f'aria-label="Link to this section">#</a>')
            out.append(
                f'<h{level} id="{anchor}">{inline(title)}{link}</h{level}>')
            i += 1
            continue

        # Tables are matched after stripping indentation: several guides nest a
        # table under a list item, and requiring the pipe at column 0 swallowed
        # those into the paragraph below them.
        if (line.lstrip().startswith("|") and i + 1 < n
                and TABLE_SEP.match(lines[i + 1].strip())):
            head_cells = [c.strip() for c in line.strip().strip("|").split("|")]
            i += 2
            rows = []
            while i < n and lines[i].lstrip().startswith("|"):
                rows.append([c.strip()
                             for c in lines[i].strip().strip("|").split("|")])
                i += 1
            thead = "".join(f"<th>{inline(c)}</th>" for c in head_cells)
            tbody = "".join(
                "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
                for r in rows)
            out.append('<div class="tablewrap"><table class="data"><thead><tr>'
                       f"{thead}</tr></thead><tbody>{tbody}</tbody></table></div>")
            continue

        if BULLET.match(line) or NUMBER.match(line):
            ordered = bool(NUMBER.match(line))
            pat = NUMBER if ordered else BULLET
            items: list[str] = []
            while i < n and pat.match(lines[i]):
                items.append(pat.match(lines[i]).group(2))
                i += 1
                # A wrapped continuation line belongs to the item above it.
                while (i < n and lines[i].strip()
                       and not pat.match(lines[i])
                       and not HEADING.match(lines[i])
                       and not lines[i].lstrip().startswith(("|", "```", ">"))
                       and lines[i].startswith((" ", "\t"))):
                    items[-1] += " " + lines[i].strip()
                    i += 1
            tag = "ol" if ordered else "ul"
            body = "".join(f"<li>{inline(x)}</li>" for x in items)
            out.append(f"<{tag}>{body}</{tag}>")
            continue

        if line.startswith(">"):
            quote = []
            while i < n and lines[i].startswith(">"):
                quote.append(lines[i].lstrip("> ").rstrip())
                i += 1
            out.append('<div class="callout callout-note"><span class="ico">note'
                       f"</span><div><p>{inline(' '.join(quote))}</p></div></div>")
            continue

        if not line.strip():
            i += 1
            continue

        # Consume the current line before testing the rest. Without that, a
        # line every branch rejects -- a pipe row whose separator line is
        # missing, so it is not a table either -- matches nothing, advances
        # nothing, and loops forever. It cost a hung build to find.
        para = [line.strip()]
        i += 1
        while (i < n and lines[i].strip()
               and not HEADING.match(lines[i])
               and not lines[i].lstrip().startswith(("|", "```", ">"))
               and not BULLET.match(lines[i]) and not NUMBER.match(lines[i])):
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(para))}</p>")

    return "\n".join(out), toc
