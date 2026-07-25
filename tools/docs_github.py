#!/usr/bin/env python3
"""Point the rendered site back at its GitHub repository.

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
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

SITE = Path("site")

# The GitHub mark (16x16, github.com/logos), a five-point star outline and a
# three-node fork glyph. Inline so the published site stays self-contained.
MARK = (
    '<svg viewBox="0 0 16 16" width="{s}" height="{s}" fill="currentColor" '
    'aria-hidden="true"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 '
    "5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94"
    "-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82."
    "72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 "
    "0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 "
    "1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 "
    "2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 "
    "0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58"
    '-8-8-8z"/></svg>'
)
STAR = (
    '<svg viewBox="0 0 16 16" width="13" height="13" fill="none" '
    'stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" '
    'aria-hidden="true"><path d="M8 1.8l1.9 3.85 4.25.62-3.08 3 .73 4.23L8 '
    '11.5l-3.8 2 .73-4.23-3.08-3 4.25-.62z"/></svg>'
)
FORK = (
    '<svg viewBox="0 0 16 16" width="13" height="13" fill="none" '
    'stroke="currentColor" stroke-width="1.5" aria-hidden="true">'
    '<circle cx="4" cy="3.5" r="1.7"/><circle cx="12" cy="3.5" r="1.7"/>'
    '<circle cx="8" cy="12.5" r="1.7"/>'
    '<path d="M4 5.2v.8a2.2 2.2 0 0 0 2.2 2.2h3.6A2.2 2.2 0 0 0 12 6V5.2M8 8.2v2.6"/>'
    "</svg>"
)

# Anchors in the generator's page shell. Absent from a page the generator did
# not emit this way; that is a layout change worth failing loudly on.
THEME_ANCHOR = b'<button class="icon-btn" id="themeBtn"'
TAIL_ANCHOR = b"</body>"
CTA_ANCHOR = b'<a class="btn" href="architecture.html">Architecture</a>'
FEATS_ANCHOR = b'<ul class="feats">'

QUICKSTART_STEPS = (
    (
        "Clone the repository",
        "",
        "git clone {url}.git",
    ),
    (
        "Set the machine up",
        "host tools, the NCS v3.3.0 toolchain, then the SDK workspace "
        "(~6.5 GB) into ./workspace — anything already installed is skipped",
        "make bootstrap",
    ),
    (
        "Build the firmware",
        "the merged image lands in ./build/merged.hex",
        "make build",
    ),
    (
        "Flash the board",
        "the first flash of a net-core image needs the erase; plain "
        "make flash after that",
        "make flash-erase",
    ),
)


def repo_slug() -> str:
    """owner/repo for the origin remote, or '' if none."""
    try:
        url = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    if not m:
        return ""
    return f"{m.group(1)}/{m.group(2)}"


def topbar_chip(slug: str, url: str) -> bytes:
    return (
        f'<a class="gh-chip" data-repo="{slug}" href="{url}" target="_blank" '
        f'rel="noopener" aria-label="{slug} on GitHub">{MARK.format(s=15)}'
        f'<span class="gh-name">{slug}</span>'
        f'<span class="gh-stat">{STAR}<b data-gh="stars"></b></span>'
        f'<span class="gh-stat">{FORK}<b data-gh="forks"></b></span></a>\n'
    ).encode()


# Styles for everything this pass injects, plus the count fetcher. One block,
# appended to every page; the quickstart rules are unused off the landing page.
def tail_block() -> bytes:
    return (
        """<style>
.gh-chip{display:inline-flex;align-items:center;gap:.55rem;padding:.3rem .8rem;border:1px solid var(--line);border-radius:99px;background:var(--surface);color:var(--ink);text-decoration:none;font-size:.78rem;transition:border-color .15s,background .15s}
.gh-chip:hover{border-color:var(--tint-line);background:var(--raise)}
.gh-chip .gh-name{font-family:var(--mono);font-weight:600}
.gh-stat{display:none;align-items:center;gap:.3rem;color:var(--muted)}
.gh-chip.ready .gh-stat{display:inline-flex}
.gh-stat b{font-weight:600;color:var(--ink);font-variant-numeric:tabular-nums}
@media (max-width:760px){.gh-chip .gh-name,.gh-chip .gh-stat{display:none!important}.gh-chip{padding:.3rem .55rem}}
.btn-gh svg{margin-right:.45rem}
.qs-steps{list-style:none;margin:0;padding:0}
.qs-steps li{display:flex;align-items:flex-start;gap:.95rem;padding:.6rem 0}
.qs-steps li+li{border-top:1px solid var(--hairline)}
.qs-n{flex:none;display:grid;place-items:center;width:1.7rem;height:1.7rem;margin-top:.3rem;border-radius:50%;background:var(--tint);color:var(--accent-ink);font-size:.78rem;font-weight:650}
.qs-b{min-width:0;flex:1}
.qs-t{font-weight:600;font-size:.92rem;margin:.15rem 0 .4rem}
.qs-t small{font-weight:500;font-size:.8rem;color:var(--muted);margin-left:.45rem}
.qs .cmdchip{margin:0;max-width:34rem}
.qs-note{font-size:.85rem;color:var(--muted);margin:.9rem 0 0}
</style>
<script>
(function(){
var chip=document.querySelector(".gh-chip");if(!chip)return;
var repo=chip.dataset.repo,key="gh-stats:"+repo;
function fmt(n){return n>=1000?(n/1000).toFixed(1).replace(/\\.0$/,"")+"k":""+n}
function show(d){chip.querySelector('[data-gh="stars"]').textContent=fmt(d.s);
chip.querySelector('[data-gh="forks"]').textContent=fmt(d.f);chip.classList.add("ready")}
var c=null;try{c=JSON.parse(localStorage.getItem(key))}catch(e){}
if(c&&typeof c.s=="number"&&Date.now()-c.t<36e5){show(c);return}
fetch("https://api.github.com/repos/"+repo).then(function(r){return r.ok?r.json():null}).then(function(j){
if(!j||typeof j.stargazers_count!="number")return;
var d={s:j.stargazers_count,f:j.forks_count,t:Date.now()};
try{localStorage.setItem(key,JSON.stringify(d))}catch(e){}
show(d)}).catch(function(){});
})();
</script>
"""
    ).encode()


def hero_button(url: str) -> bytes:
    return (
        f'<a class="btn btn-gh" href="{url}" target="_blank" rel="noopener">'
        f"{MARK.format(s=15)}GitHub</a>"
    ).encode()


def quickstart(url: str) -> bytes:
    items = []
    for n, (title, aside, cmd) in enumerate(QUICKSTART_STEPS, 1):
        cmd = cmd.format(url=url)
        small = f" <small>{aside}</small>" if aside else ""
        items.append(
            f'<li><span class="qs-n">{n}</span><div class="qs-b">'
            f'<div class="qs-t">{title}{small}</div>'
            f'<div class="cmdchip"><span class="t-p">$</span>'
            f'<span class="c-cmd">{cmd}</span>'
            f'<button type="button" class="js-copycmd" data-cmd="{cmd}">Copy'
            f"</button></div></div></li>"
        )
    steps = "\n".join(items)
    return (
        f"""<section class="qs" id="get-running">
<div class="section-h"><h2>Get running</h2><span class="rule"></span></div>
<ol class="qs-steps">
{steps}
</ol>
<p class="qs-note">No toolchain? <code>make test</code> runs as-is. On ESP32-S3,
start at the <a href="esp32-bringup.html">bring-up checklist</a>.</p>
</section>
""".encode()
    )


def main() -> int:
    index = SITE / "index.html"
    if not index.is_file():
        print("    no rendered site — nothing to link")
        return 0

    slug = repo_slug()
    if not slug:
        print("    no origin remote — repository links skipped")
        return 0
    url = f"https://github.com/{slug}"

    chip = topbar_chip(slug, url)
    tail = tail_block()
    chipped = tailed = kept = 0
    for page in sorted(SITE.glob("*.html")):
        raw = page.read_bytes()
        out = raw
        if b'class="gh-chip"' in out:
            kept += 1
        elif THEME_ANCHOR not in out:
            print(
                f"docs_github: {page.name} has no theme toggle to anchor the "
                "repository chip on — generator layout changed?",
                file=sys.stderr,
            )
            return 1
        else:
            out = out.replace(THEME_ANCHOR, chip + THEME_ANCHOR, 1)
            chipped += 1
        if b".gh-chip{" not in out and TAIL_ANCHOR in out:
            out = out.replace(TAIL_ANCHOR, tail + TAIL_ANCHOR, 1)
            tailed += 1
        if out is not raw:
            page.write_bytes(out)
    note = f" ({kept} already linked)" if kept else ""
    print(f"    repository chip on {chipped} page(s){note} -> {url}")

    raw = index.read_bytes()
    if b'class="btn btn-gh"' in raw:
        print("    hero GitHub button already present")
    elif CTA_ANCHOR not in raw:
        print(
            "docs_github: landing page has no Architecture call-to-action to "
            "anchor the GitHub button on — generator layout changed?",
            file=sys.stderr,
        )
        return 1
    else:
        raw = raw.replace(CTA_ANCHOR, CTA_ANCHOR + hero_button(url), 1)
        print("    hero GitHub button injected")

    if b'class="qs-steps"' in raw:
        print("    quickstart already present")
    elif FEATS_ANCHOR not in raw:
        print(
            "docs_github: landing page has no explore-card list to anchor the "
            "quickstart on — generator layout changed?",
            file=sys.stderr,
        )
        return 1
    else:
        raw = raw.replace(FEATS_ANCHOR, quickstart(url) + FEATS_ANCHOR, 1)
        print("    quickstart injected into index.html")
    index.write_bytes(raw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
