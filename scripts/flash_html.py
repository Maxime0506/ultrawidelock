#!/usr/bin/env python3
"""Render a release FLASH.md into a self-contained FLASH.html.

The markdown file stays the single source of truth; this wraps its rendered
body in an embedded stylesheet (light + dark, no external assets) so the
bundle ships a guide that reads well in a browser. The output is committed
next to its source, so regenerate after editing a FLASH.md:

    pip install markdown==3.8
    python3 scripts/flash_html.py release/*/FLASH.md

Output is deterministic (no timestamps): it only changes when the source does.
"""

import pathlib
import re
import sys

import markdown

STYLE = """
:root {
  color-scheme: light dark;
  --bg: #f5f6f5; --panel: #ffffff; --ink: #20282d; --head: #131a1e;
  --muted: #5f6d75; --line: #dfe4e6; --accent: #0b7d9c;
  --code-bg: #f0f3f4; --warn: #b3421f;
  --sans: -apple-system, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
  --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #171c20; --panel: #1e2429; --ink: #ccd6db; --head: #eef4f7;
    --muted: #8b9ba4; --line: #2c363d; --accent: #4cb8d4;
    --code-bg: #12171b; --warn: #e0764f;
  }
}
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--bg); }
body { font-family: var(--sans); color: var(--ink); line-height: 1.55; font-size: 16px; padding: 0 1.5rem; }
main { max-width: 46rem; margin: 0 auto; padding: 3rem 0 5rem; }
.eyebrow { font-family: var(--mono); font-size: .7rem; letter-spacing: .12em; text-transform: uppercase; color: var(--muted); margin: 0 0 .8rem; }
.eyebrow b { color: var(--accent); font-weight: 600; }
h1 { font-size: 1.75rem; line-height: 1.2; margin: 0 0 .5rem; color: var(--head); text-wrap: balance; letter-spacing: -.01em; }
h1 + p { color: var(--muted); font-size: .95rem; }
h2 { font-size: .78rem; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); margin: 2.8rem 0 .6rem; font-weight: 600; }
h3 { font-size: 1rem; color: var(--head); margin: 1.6rem 0 .4rem; }
p, li { font-size: .92rem; }
p { margin: .75rem 0; }
ul, ol { padding-left: 1.25rem; margin: .75rem 0; }
li { margin-bottom: .3rem; }
li > p { margin: .3rem 0; }
strong { color: var(--head); }
a { color: var(--accent); }
:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
code { font-family: var(--mono); font-size: .84em; }
p code, li code, td code { background: var(--code-bg); border: 1px solid var(--line); border-radius: 4px; padding: .06em .3em; }
.prewrap { position: relative; margin: 1rem 0; }
pre { background: var(--code-bg); border: 1px solid var(--line); border-radius: 6px; padding: .85rem 1rem; overflow-x: auto; font-family: var(--mono); font-size: .78rem; line-height: 1.55; margin: 0; }
pre code { background: none; border: 0; padding: 0; }
.copy { position: absolute; top: .5rem; right: .5rem; border: 1px solid var(--line); background: var(--panel); color: var(--muted); border-radius: 5px; font-family: var(--sans); font-size: .68rem; padding: .2rem .55rem; opacity: 0; cursor: pointer; transition: opacity .15s, color .15s, border-color .15s; }
.prewrap:hover .copy, .copy:focus-visible { opacity: 1; }
.copy.done { color: var(--accent); border-color: var(--accent); opacity: 1; }
.tablewrap { overflow-x: auto; margin: 1rem 0; }
table { border-collapse: collapse; width: 100%; font-size: .88rem; font-variant-numeric: tabular-nums; }
th { text-align: left; font-size: .68rem; letter-spacing: .07em; text-transform: uppercase; color: var(--muted); font-weight: 600; }
th, td { padding: .45rem 1rem .45rem 0; border-bottom: 1px solid var(--line); vertical-align: top; }
tr:last-child td { border-bottom: none; }
blockquote { border-left: 3px solid var(--warn); padding: .1rem 0 .1rem .9rem; margin: 1rem 0; }
blockquote p { margin: .5rem 0; }
blockquote strong { color: var(--warn); }
footer { margin-top: 3.5rem; padding-top: 1rem; border-top: 1px solid var(--line); color: var(--muted); font-size: .84rem; }
"""

# Copy buttons on the command blocks.
SCRIPT = """
document.querySelectorAll("pre").forEach(function(pre){
  var wrap=document.createElement("div");wrap.className="prewrap";
  pre.parentNode.insertBefore(wrap,pre);wrap.appendChild(pre);
  var btn=document.createElement("button");btn.className="copy";btn.type="button";
  btn.textContent="Copy";wrap.appendChild(btn);
  btn.addEventListener("click",function(){
    navigator.clipboard.writeText(pre.textContent.replace(/\\n$/,"")).then(function(){
      btn.textContent="Copied";btn.classList.add("done");
      setTimeout(function(){btn.textContent="Copy";btn.classList.remove("done");},1600);
    });
  });
});
"""

TEMPLATE = """<!doctype html>
<!-- Generated from FLASH.md by scripts/flash_html.py; edit the .md, not this. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<!-- These pages are wholly self-contained: one inline <style>, no script, no remote
     subresource. So the policy can be the strictest one that still renders, and any future
     edit that reaches for a CDN or an inline script fails in the browser rather than shipping.
     GitHub Pages sends no CSP header and cannot be configured to, so a meta tag is the only
     place a policy can exist for the published copy. -->
<meta http-equiv="Content-Security-Policy"
      content="default-src 'none'; style-src 'unsafe-inline'; img-src 'self' data:;
               base-uri 'none'; form-action 'none'; frame-ancestors 'none'">
<title>{title}</title>
<style>{style}</style>
</head>
<body>
<main>
<p class="eyebrow"><b>openaliro</b> · flash bundle · evaluation firmware</p>
{body}
<footer>Same content as this bundle's <code>FLASH.md</code>.
<a href="https://github.com/asxeem/openaliro">github.com/asxeem/openaliro</a></footer>
</main>
<script>{script}</script>
</body>
</html>
"""


def render(src: pathlib.Path) -> pathlib.Path:
    """Convert a Markdown source file to an HTML page: extract the first H1 as the title (fallback to parent directory name), render Markdown with table and fenced code support, reformat section labels from "## N. TEXT" to "N · TEXT", wrap tables in a scrollable container for mobile, and write to a .html file in the same directory.
    Returns the path to the rendered HTML file.
    """
    text = src.read_text(encoding="utf-8")
    title = next(
        (ln[2:].strip() for ln in text.splitlines() if ln.startswith("# ")),
        src.parent.name,
    )
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    # "## 1. What you need" -> "1 · WHAT YOU NEED" section labels.
    body = re.sub(r"(<h2>)(\d+)\. ", r"\1\2 · ", body)
    # Wide pin tables must scroll inside their own box on a phone.
    body = body.replace("<table>", '<div class="tablewrap"><table>')
    body = body.replace("</table>", "</table></div>")
    out = src.with_suffix(".html")
    out.write_text(
        TEMPLATE.format(title=title, style=STYLE, body=body, script=SCRIPT),
        encoding="utf-8",
    )
    return out


def main() -> int:
    """Convert a Markdown file to HTML: extract title from first H1 (or parent directory name), render Markdown with code fence and table support, rewrite section headers from "## N. TEXT" to "N · TEXT", wrap tables for mobile scrolling. Write output as .html alongside the source."""
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for arg in sys.argv[1:]:
        print(render(pathlib.Path(arg)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
