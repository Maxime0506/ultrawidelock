#!/usr/bin/env python3
"""Give the rendered site a real "Get started" landing.

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
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SITE = Path("site")
TEMPLATE = SITE / "RELEASING.html"
START = SITE / "start.html"

CTA_RE = re.compile(r'(<a class="btn btn-primary" href=")[^"]+(">Get started)')
NAV_RE = re.compile(r"^const NAV=(\{.*?\});$", re.M)
NAV_ANCHOR = '<script defer src="nav.js"></script>'


def repo_url() -> str:
    """Return the GitHub URL (https://github.com/owner/repo) of the origin remote, or an empty string if the remote is not configured or not a GitHub URL."""
    try:
        url = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    return f"https://github.com/{m.group(1)}/{m.group(2)}" if m else ""


def chip(cmd: str) -> str:
    """Render a shell command in a copyable chip with a Copy button and dollar-sign prompt. No comments allowed inline; context goes in surrounding prose."""
    # Command only — no `# comment` in or next to anything copyable. Context
    # goes in prose around the chip instead.
    return (
        f'<div class="cmdchip"><span class="t-p">$</span>'
        f'<span class="c-cmd">{cmd}</span>'
        f'<button type="button" class="js-copycmd" data-cmd="{cmd}">Copy</button></div>'
    )


def row(href: str, name: str, desc: str) -> str:
    """Render a navigation card row with a link, title, and description text."""
    return (
        f'<li><a href="{href}"><span class="row-name">{name}</span>'
        f'<span class="row-desc">{desc}</span></a></li>'
    )


ICONS = {
    "chip": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 3v2m6-2v2M9 19v2m6-2v2M3 9h2m-2 6h2M19 9h2m-2 6h2"/></svg>',
    "dl": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M12 3v12m0 0 4-4m-4 4-4-4"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></svg>',
    "play": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M8 5v14l11-7z"/></svg>',
    "layers": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M12 3 2 8.5 12 14l10-5.5L12 3z"/><path d="M2 15.5 12 21l10-5.5"/></svg>',
    "radio": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><circle cx="12" cy="12" r="2"/><path d="M7.8 16.2a6 6 0 0 1 0-8.4m8.4 0a6 6 0 0 1 0 8.4M4.9 19.1a10 10 0 0 1 0-14.2m14.2 0a10 10 0 0 1 0 14.2"/></svg>',
    "branch": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="6" cy="5" r="2.2"/><circle cx="6" cy="19" r="2.2"/><circle cx="18" cy="9" r="2.2"/><path d="M6 7.2v9.6M18 11.2c0 3-3 4-6 4"/></svg>',
    "bolt": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M13 2 4.5 13.5H11L9.5 22 19 10h-6.5z"/></svg>',
    # The disclosure arrows were the "⌄" character, which every platform draws
    # at a different weight and baseline; a stroked path is the same everywhere.
    "chev": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m6 9 6 6 6-6"/></svg>',
}

# A track is a card, not a row: the icon and the track's number sit on one
# line and the title block wraps under them, which gives the serif title the
# width to breathe and leaves the closed grid reading as six equal choices.
# The number is a counter rather than markup, so reordering the tracks below
# renumbers them. `.path` is the spotlight host — docs_hero.py feeds it --mx
# and --my from the pointer, exactly as it does the landing page's cards.
STYLE = """<style>
/* The band's own styling is docs_hero.py's; these are the shapes, so the page
   still holds together if only this pass has run. */
.start-meta{display:flex;flex-wrap:wrap;gap:.5rem;margin-top:1.6rem}
.start-meta span{font-size:.76rem;color:var(--muted);border:1px solid var(--line);
  border-radius:99px;padding:.32rem .75rem;background:var(--card)}
/* The sitewide 4.4rem above a section rail separates two sections; here the
   band is directly above it and does the separating already. */
.doc .section-h:first-of-type{margin-top:.4rem}
.paths{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem;
  margin:1.4rem 0 2.4rem;counter-reset:pathn}
@media(max-width:860px){.paths{grid-template-columns:1fr}}
/* opacity is in the list because the reveal layer fades these in and this
   rule, being the later one, is the transition they actually get. */
.path{position:relative;overflow:hidden;counter-increment:pathn;
  border:1px solid var(--line);border-radius:16px;background:var(--card);
  transition:opacity .4s ease,border-color .18s ease,box-shadow .18s ease,
    transform .3s cubic-bezier(.2,.7,.2,1)}
.path::before{content:"";position:absolute;inset:0;opacity:0;pointer-events:none;
  transition:opacity .3s ease;background:radial-gradient(14rem 14rem at
    var(--mx,50%) var(--my,50%),var(--tint),transparent 70%)}
.path:hover::before{opacity:1}
.path:hover{border-color:var(--tint-line);box-shadow:var(--shadow-hover);transform:translateY(-2px)}
.path[open],.path[open]:hover{grid-column:1/-1;border-color:var(--tint-line);
  box-shadow:var(--shadow);transform:none}
.path[open]::after{content:"";position:absolute;left:0;top:0;bottom:0;width:2px;
  background:linear-gradient(180deg,var(--accent),transparent 70%)}
.path>summary{position:relative;display:flex;flex-wrap:wrap;align-items:center;
  gap:.85rem;padding:1.2rem 1.25rem;cursor:pointer;list-style:none}
.path>summary::-webkit-details-marker{display:none}
.path>summary::after{content:counter(pathn,decimal-leading-zero);order:1;
  margin-left:auto;padding-right:2rem;font-family:var(--mono);font-size:.74rem;
  letter-spacing:.1em;color:var(--faint)}
/* The reference tracks are not a fourth, fifth and sixth route, so they are
   not numbered as if the reader had to get through them in order. */
.paths.deeper .path>summary::after{content:none}
.p-ico{order:0;flex:none;display:grid;place-items:center;width:2.6rem;height:2.6rem;
  border-radius:12px;color:var(--accent-ink);
  background:linear-gradient(150deg,var(--tint),transparent 78%),var(--surface);
  box-shadow:inset 0 0 0 1px var(--tint-line);
  transition:transform .24s cubic-bezier(.2,.7,.2,1)}
.path:hover .p-ico{transform:scale(1.06) rotate(-3deg)}
.p-ico svg{width:1.3rem;height:1.3rem}
.p-t{order:2;flex-basis:100%;min-width:0;margin-top:.2rem}
/* What the route costs, said before its name: the whole page is ordered by
   commitment, so the price is the thing being chosen between. */
.p-when{display:inline-block;margin-bottom:.45rem;padding:.18rem .58rem;
  border:1px solid var(--tint-line);border-radius:99px;background:var(--tint);
  color:var(--accent-ink);font-family:var(--mono);font-size:.66rem;
  letter-spacing:.09em;text-transform:uppercase}
.p-t b{display:block;font-family:var(--serif);font-size:1.28rem;font-weight:500;
  line-height:1.24;color:var(--strong)}
.p-t small{display:block;margin-top:.32rem;font-size:.85rem;line-height:1.45;color:var(--muted)}
.p-chev{position:absolute;right:1.25rem;top:1.7rem;display:grid;place-items:center;
  width:1.6rem;height:1.6rem;border-radius:50%;border:1px solid var(--line);
  background:var(--surface);color:var(--faint);
  transition:transform .24s ease,color .16s ease,border-color .16s ease}
.p-chev svg{width:.9rem;height:.9rem}
.path:hover .p-chev{color:var(--accent-ink);border-color:var(--tint-line)}
.path[open] .p-chev{transform:rotate(180deg);color:var(--accent-ink);border-color:var(--tint-line)}
.p-body{position:relative;padding:.35rem 1.25rem 1.25rem;border-top:1px solid var(--hairline)}
.path[open] .p-body{animation:p-in .28s cubic-bezier(.2,.7,.2,1) both}
@keyframes p-in{from{opacity:0;transform:translateY(-5px)}}
.p-sub{border:1px solid var(--hairline);border-radius:12px;margin:.7rem 0;background:var(--surface)}
.p-sub summary{padding:.62rem .85rem;font-size:.85rem;font-weight:600;cursor:pointer;list-style:none;display:flex;align-items:center}
.p-sub summary::-webkit-details-marker{display:none}
.p-sub summary::after{content:"";margin-left:auto;flex:none;width:.62rem;height:.62rem;
  border-right:1.6px solid var(--faint);border-bottom:1.6px solid var(--faint);border-radius:1px;
  transform:translateY(-.14rem) rotate(45deg);transition:transform .2s ease}
.p-sub[open] summary::after{transform:translateY(.08rem) rotate(225deg)}
.p-sub .s-body{padding:.1rem .85rem .8rem;font-size:.88rem;color:var(--muted)}
.p-sub .s-body p{margin:.45rem 0}
.p-body .cmdchip{margin:.5rem 0;max-width:none}
.p-body .rows{margin:.4rem 0}
.p-body .rows a{border-radius:9px;padding-left:.6rem;padding-right:.6rem;
  transition:background-color .14s ease}
.p-body .rows a:hover{background:var(--tint)}
.p-body>p{font-size:.9rem;color:var(--muted);margin:.6rem 0 .2rem}
@media(prefers-reduced-motion:reduce){
  .path,.path::before,.p-ico,.p-chev{transition:none}
  .path[open] .p-body{animation:none}}
</style>"""

# Three facts, all of them answerable from the routes below: what the cheapest
# way in costs, which targets they cover, and that the first one needs nothing
# on the desk at all.
HERO = (
    '<header class="hero-band"><div class="hero-in">'
    '<div class="eyebrow">Start here</div><h1>Get started</h1>'
    '<p class="lede">Three routes in, ordered by what they cost you: watch the '
    "firmware run in a browser tab, flash a real lock without a toolchain, or "
    "build the whole thing. Pick one; it opens in place.</p>"
    '<div class="start-meta"><span>Route 1 needs no hardware</span>'
    "<span>DWM3001CDK &middot; nRF5340 DK &middot; ESP32-S3/C5/C6</span>"
    "<span>Host tests need no board</span></div>"
    "</div></header>"
)

# Every page and anchor referenced here is validated by the link pass that
# runs after this one, so a renamed guide fails the build instead of rotting.
def routes_html(gh: str) -> list[tuple[str, str, str, str, str]]:
    """Return the three getting-started routes as (icon, cost, title, subtitle, body) tuples, ordered by what each one asks of the reader: a browser tab, a board and no toolchain, then a full checkout. Embeds the provided GitHub URL into clone and release rows."""
    routes = []
    routes.append((
        "play", "0 minutes &middot; no hardware", "Watch it run",
        "The firmware itself, compiled to WASM, in a browser tab", (
            '<ul class="rows">'
            + row("twin.html", "Digital twin",
                  "A lock and a phone on one page: walk the phone in and watch "
                  "the ranging round, the RSSI gate and the approach controller "
                  "decide.")
            + "</ul>"
            "<p>Not a mock-up of the decision: the twin runs the same "
            "<code>modules/woz_uwb</code> logic the board runs, compiled to "
            "WASM, and CI rebuilds it and byte-diffs the result so the page "
            "cannot drift from the firmware.</p>"
            '<ul class="rows">'
            + row("protocol-research.html", "How the unlock works",
                  "The BLE and UWB exchange the twin is replaying, as observed "
                  "on air.")
            + "</ul>"
        )))

    rel_row = row(f"{gh}/releases", "Release bundles",
                  "Prebuilt images for every target, if you would rather not "
                  "flash from the browser.") if gh else ""
    routes.append((
        "bolt", "~10 minutes &middot; no toolchain", "Flash a real lock",
        "An ESP32 written straight from this site, then added to Apple Home", (
            # docs_flash.py puts the browser-flasher row at the head of this
            # list, and only when an image was actually staged next to the site.
            '<ul class="rows" id="flash-slot">'
            + row("add-the-key.html", "Add the key",
                  "Commissioning, for every target: the setup code, the "
                  "uncertified-accessory warning, and what a healthy pairing "
                  "looks like.")
            + row("esp32-bringup.html", "ESP32 bring-up (S3, C5, and C6)",
                  "DWM3000EVB to the board, pin by pin, and what good output "
                  "looks like — the table CI keeps in sync with "
                  "<code>board_pins.h</code>.")
            + rel_row
            + "</ul>"
            "<p>What this needs on the desk: an ESP32-S3, C5 or C6 dev board, a "
            "Qorvo DWM3000EVB, eleven jumper wires and a USB data cable. "
            "Chrome, Edge or Firefox on a computer does the writing over "
            "WebSerial.</p>"
            "<p>Apple\u2019s side: an iPhone with a UWB chip, a home hub, and a "
            "2.4&nbsp;GHz Wi-Fi network for the board to join. No ESP-IDF, no "
            "clone, nothing to install.</p>"
            '<details class="p-sub"><summary>What is actually validated on '
            'each chip</summary><div class="s-body">'
            "<p><b>ESP32-S3</b>: approach unlock, on hardware.</p>"
            "<p><b>ESP32-C6</b>: approach unlock, on hardware; it drives a BU04 "
            "over direct SPI.</p>"
            "<p><b>ESP32-C5</b>: builds and releases, no hardware validation "
            "recorded.</p>"
            "<p>None of the three has an NFC tap path. That needs the "
            "nRF5340 DK.</p></div></details>"
        )))

    clone = (chip(f"git clone {gh}.git") + chip("cd openaliro")) if gh else ""
    routes.append((
        "chip", "a full dev setup", "Build it yourself",
        "Clone, bootstrap the toolchain, and flash the board you have", (
            '<details class="p-sub" open><summary>DWM3001CDK &mdash; the primary '
            "target</summary>"
            '<div class="s-body">'
            + clone
            + chip("make dfu-key")
            + chip("make bootstrap")
            + chip("make build")
            + chip("make flash")
            + chip("make monitor")
            + "<p>One nRF52833 and the DW3110 in the same module: nothing to "
              "wire, an on-board J-Link, and the reader, a Matter node and a "
              "Thread MTD in one image. Bare targets always mean this board; "
              "the image lands in <code>build/cdk-matter</code>.</p>"
              "<p><code>make dfu-key</code> is not optional and not "
              "once-per-machine. Every image is signed, the key is gitignored, "
              "and without one the build fails at configure rather than falling "
              "back to MCUboot\u2019s published demo key \u2014 so a fresh clone "
              "or a new git worktree needs its own.</p>"
              "<p><code>make bootstrap</code> is the ~8.5&nbsp;GB one: host "
              "tools, the pinned NCS v3.3.0 toolchain, then NCS itself into "
              "<code>./workspace</code>. An existing toolchain is detected and "
              "skipped. <code>make monitor</code> is RTT over "
              "<code>probe-rs</code>; this board has no UART console at "
              "all.</p>"
            + '<ul class="rows">'
            + row("../firmware/README.md", "The DWM3001CDK manual",
                  "Sizes, partitions, the RTT console, the update over "
                  "Bluetooth, and the per-stage hardware status.")
            + row("dwm3001cdk-surgery.html", "Bring-up traps",
                  "Every trap this target set, with the symptom that exposed "
                  "it. Read it before debugging.")
            + "</ul></div></details>"
            '<details class="p-sub"><summary>nRF5340 DK &mdash; the target with '
            "the NFC tap</summary>"
            '<div class="s-body">'
            + chip("make nrf-build")
            + chip("make nrf-flash-erase")
            + chip("make nrf-term")
            + "<p>A DK plus a DWM3000EVB shield, and the only target with a "
              "reader IC, so Express Mode tap works here and nowhere else. "
              "<code>make nrf-flash-erase</code> is required after any "
              "net-core config change.</p>"
            + '<ul class="rows">'
            + row("nrf5340-bringup.html", "nRF5340 bring-up",
                  "First flash, the shell, and what a healthy boot looks like.")
            + row("nrf5340-wiring.html", "nRF5340 wiring",
                  "The shield and the NFC reader, pin by pin.")
            + "</ul></div></details>"
            '<details class="p-sub"><summary>ESP32-S3, C5 and C6</summary>'
            '<div class="s-body">'
            + chip("make esp-build APP=matter-lock TARGET=esp32s3")
            + "<p>ESP-IDF and esp-matter rather than NCS, with the same shared "
              "<code>modules/</code> underneath. <code>TARGET</code> takes "
              "<code>esp32s3</code>, <code>esp32c5</code> or "
              "<code>esp32c6</code>; <code>APP=reader</code> builds the "
              "standalone bench reader instead of the full lock.</p>"
            + '<ul class="rows">'
            + row("esp32-bringup.html", "ESP32 bring-up (S3, C5, and C6)",
                  "Wiring, first boot, and how to prove a real range.")
            + row("esp32-gotchas.html", "ESP32 gotchas",
                  "The numbered list of what this port got wrong first.")
            + "</ul></div></details>"
            '<details class="p-sub"><summary>No board yet? The host suites need '
            "none</summary>"
            '<div class="s-body">'
            + chip("make test")
            + chip("make verify")
            + "<p><code>make test</code> is the host KAT suite: no NCS "
              "toolchain, no hardware. <code>make verify</code> is the pre-PR "
              "sweep \u2014 every CI gate a host can run, in about 35 seconds, "
              "and the same script CI itself calls.</p></div></details>"
            + '<ul class="rows">'
            + row("set-up.html", "Installing",
                  "The full install guide \u2014 every target, every knob.")
            + row("configuring.html", "Configuring",
                  "Build options, Kconfig overlays, and the runtime consoles.")
            + row("add-the-key.html", "Add the key",
                  "Commissioning into Apple Home, once something is flashed.")
            + row("hardware-validation.html", "Hardware validation",
                  "What to prove on the bench that automated CI cannot.")
            + row("troubleshooting.html", "Troubleshooting",
                  "Symptoms, grouped by target, leading with the CDK.")
            + "</ul>"
            '<p>The step-by-step version of this route sits on the '
            '<a href="index.html#get-running">landing page</a>.</p>'
        )))
    return routes


def deeper_html(gh: str) -> list[tuple[str, str, str, str, str]]:
    """Return the reference tracks shown under the three routes as (icon, cost, title, subtitle, body) tuples, for a reader who is already running. The cost field is empty: these are not a fourth route. Embeds the provided GitHub URL into the repository rows."""
    tracks = []
    tracks.append(("layers", "", "Firmware internals",
                   "How the reader is put together", (
        '<ul class="rows">'
        + row("architecture.html", "Architecture",
              "The module graph, color-keyed by subsystem, and every "
              "module\u2019s declarations.")
        + row("modules.html", "Modules",
              "Every file in the tree, grouped by directory, with the brief "
              "from its own docstring.")
        + row("chipset-memory.html", "Memory usage",
              "Where the build\u2019s flash and RAM go, measured.")
        + row("reference.html", "Reference",
              "The Doxygen tree, generated from the declarations themselves.")
        + "</ul>"
    )))
    tracks.append(("radio", "", "Protocol &amp; research",
                   "How the unlock actually works on air", (
        '<ul class="rows">'
        + row("protocol-research.html", "Protocol research",
              "BLE + UWB proximity unlock, as observed on air.")
        + row("protocol-notes.html", "Time synchronization",
              "Wall-clock time and credential validity in the firmware.")
        + row("approach-direction.html", "Approach Direction",
              "The Home app\u2019s Left/Front/Right control, end to end.")
        + row("range-integrity.html", "Range integrity",
              "What a signed distance is worth, and what defends it.")
        + row("porting-esp32-phase3.html", "Deriving the ranging key",
              "The credential auth, phase by phase.")
        + "</ul>"
    )))
    gh_rows = ""
    if gh:
        gh_rows = (
            row(gh, "Repository", "Source, issues and pull requests on GitHub.")
            + row(f"{gh}/issues", "Issues", "Report a bug or pick something up.")
        )
    tracks.append(("branch", "", "Project &amp; contributing",
                   "CI, releasing, and where the work happens", (
        '<ul class="rows">' + gh_rows
        + row("RELEASING.html", "Releasing", "How a release is cut and what gates it.")
        + row("porting.html", "Porting openaliro",
              "What moving the engine to a new chipset costs, and how to prove it.")
        + "</ul>"
        '<details class="p-sub" open><summary>What CI checks, and how to run it '
        "here</summary>"
        '<div class="s-body"><p>Host tests with a coverage floor, ASan/UBSan '
        "sanitizer runs, clang-format and clang-tidy, shell and workflow lint, "
        "fuzzing, CBMC proofs, port tests, and the blocking security "
        "gates.</p>"
        + chip("make tools")
        + chip("make verify")
        + "<p>CI is one job and it runs <code>make verify</code>, so a green "
          "sweep and a green PR mean the same thing. <code>make tools</code> "
          "says what each gate needs and what is missing here; a gate whose "
          "tool is absent fails the sweep rather than passing quietly, because "
          "CI runs it either way. Firmware images are never built on a push: "
          "that workflow is dispatch-only, because it needs the full "
          "toolchains.</p></div></details>"
    )))
    return tracks


def card(ico: str, when: str, title: str, sub: str, body: str) -> str:
    """Render one route or track as a collapsible summary card. An empty cost field omits the badge, which is what separates a reference track from a numbered route."""
    badge = f'<span class="p-when">{when}</span>' if when else ""
    return (
        f'<details class="path"><summary><span class="p-ico">{ICONS[ico]}</span>'
        f'<span class="p-t">{badge}<b>{title}</b><small>{sub}</small></span>'
        f'<span class="p-chev">{ICONS["chev"]}</span></summary>'
        f'<div class="p-body">{body}</div></details>'
    )


def cards(items: list[tuple[str, str, str, str, str]]) -> str:
    """Render a list of route or track tuples as one run of cards."""
    return "".join(card(*item) for item in items)


def main_html(gh: str) -> str:
    """Render the main content section of the Get-Started landing page: the three escalating-commitment routes, then the reference tracks for a reader who is already running. Embeds the provided GitHub URL into clone, release and repository rows. Returns HTML."""
    return (
        f'<main class="doc">\n{STYLE}\n'
        f'<div class="section-h"><h2>Pick a route</h2><span class="rule"></span></div>\n'
        f'<div class="paths">{cards(routes_html(gh))}</div>\n'
        f'<div class="section-h"><h2>Once it is running</h2>'
        f'<span class="rule"></span></div>\n'
        f'<div class="paths deeper">{cards(deeper_html(gh))}</div>\n'
        f"</main>"
    )


# Sitewide: the sidebar Get-started entry, and the search control promoted to
# the visual weight of a primary action — tinted, briefly pinging on load,
# with a springier palette open. Injected after nav.js so the tree exists.
WAYFIND = """<style>
.search-btn{background:var(--tint);border-color:var(--tint-line);color:var(--accent-ink);font-weight:600;animation:gv-ping 1.9s ease-out .9s 2}
.search-btn:hover{border-color:var(--accent);color:var(--accent-ink);box-shadow:0 0 0 3px var(--tint)}
.search-btn kbd{color:var(--accent-ink);border-color:var(--tint-line);background:var(--surface)}
@keyframes gv-ping{0%{box-shadow:0 0 0 0 var(--tint-line)}100%{box-shadow:0 0 0 10px transparent}}
.palette{transition:opacity .16s ease,transform .22s cubic-bezier(.2,.9,.3,1.15)}
.topbar .js-search{color:var(--accent-ink)}
</style>
<script id="gv-start">
(function(){function go(){var dl=document.querySelector(".tree .doclinks");
if(!dl||dl.querySelector('a[href="start.html"]'))return;
var a=document.createElement("a");a.href="start.html";
a.className="doclink"+(location.pathname.split("/").pop()==="start.html"?" on":"");
a.innerHTML='__BOLT__';
a.appendChild(document.createTextNode("Get started"));
var links=dl.querySelectorAll(".doclink");
dl.insertBefore(a,links[1]||null)}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go()})();
</script>""".replace("__BOLT__", ICONS["bolt"])


def build_page(template: str, gh: str) -> str:
    """Build the Get-Started page by injecting main_html(gh) into the site template, then update the hero button link and search index. Applies title, og:title, breadcrumb, and active-nav-marker rewrites. Returns the modified page as a string."""
    page = template
    page = re.sub(
        r"<title>[^<]*</title>", "<title>Get started</title>", page, count=1
    )
    page = re.sub(
        r'(<meta property="og:title" content=")[^"]*(")',
        r"\1Get started\2", page, count=1,
    )
    page = re.sub(r'data-active="[^"]*"', 'data-active="start"', page, count=1)
    page = re.sub(
        r'<div class="crumb">.*?</div>',
        '<div class="crumb"><b>Get started</b></div>',
        page, count=1, flags=re.S,
    )
    # function replacements: the injected HTML must land verbatim, not be
    # parsed as a replacement template (a CSS "\\23.." would read as octal)
    page = re.sub(
        r'<header class="hero-band">.*?</header>', lambda m: HERO,
        page, count=1, flags=re.S,
    )
    body = main_html(gh)
    return re.sub(
        r'<main class="doc[^"]*">.*?</main>', lambda m: body, page,
        count=1, flags=re.S,
    )


def add_search_row(page_name: str) -> None:
    """Add an entry for the given page name to the nav.js search array if not already present. Inserts at position 1 (after the index entry) and rewrites the JSON in place. Does nothing silently if nav.js is absent."""
    nav_path = SITE / "nav.js"
    if not nav_path.is_file():
        return
    text = nav_path.read_text()
    m = NAV_RE.search(text)
    if not m:
        return
    nav = json.loads(m.group(1))
    entry = ["page", "Get started", "", page_name]
    if entry not in nav.get("search", []):
        nav["search"].insert(1, entry)
        nav_path.write_text(
            text[: m.start()] + "const NAV=" + json.dumps(nav) + ";" + text[m.end() :]
        )


def main() -> int:
    """Render the Get-Started landing page and inject it into site/start.html, wire the wayfinding guide into every page, and update navigation and hero-button references. Reports counts of pages modified and returns 1 if the template layout has changed. Requires the site to be rendered first."""
    if not TEMPLATE.is_file():
        print("    no rendered site — nothing to build the landing from")
        return 0

    gh = repo_url()
    template = TEMPLATE.read_text()
    for marker in ("hero-band", NAV_ANCHOR):
        if marker not in template:
            print(
                f"docs_start: template page lacks {marker!r} — generator "
                "layout changed?", file=sys.stderr,
            )
            return 1
    START.write_text(build_page(template, gh))
    print("    start.html built")

    index = SITE / "index.html"
    if index.is_file():
        idx, n = CTA_RE.subn(r"\1start.html\2", index.read_text())
        if n:
            index.write_text(idx)
            print("    hero Get-started button now opens start.html")

    add_search_row(START.name)

    wired = kept = 0
    for p in sorted(SITE.glob("*.html")):
        content = p.read_text()
        if 'id="gv-start"' in content:
            kept += 1
            continue
        if NAV_ANCHOR not in content:
            continue
        p.write_text(content.replace(NAV_ANCHOR, NAV_ANCHOR + WAYFIND, 1))
        wired += 1
    note = f" ({kept} already wired)" if kept else ""
    print(f"    wayfinding shim on {wired} page(s){note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
