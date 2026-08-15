# web/

The site, the browser flasher, the digital twin, and the subsystem graph.

```sh
python3 web/build.py --check      # build web/dist/, fail on any dead link
```

Stdlib Python only. No node, no bundler, nothing to install.

## What is here

| Path | What it is |
|---|---|
| `build.py` | the whole generator, and the link gate |
| `site/` | landing and docs page sources |
| `flasher/` | WebSerial flasher for the ESP32 lock |
| `twin/` | the walk-up digital twin, firmware logic compiled to WASM |
| `graph/` | subsystem graph, generated from the source tree |
| `assets/design/` | vendored design system: tokens, components, theme toggle |
| `dist/` | **generated, gitignored, disposable** |

## Two rules

**Nothing generated is committed.** The pipeline this replaces wrote one page
per source file into `docs/` and committed all 476 of them. Those pages carried
derived line numbers, so every merge conflicted on lines no resolution could
settle correctly. It needed a `make sync` target whose own comment admitted
that "only a regeneration can" fix it. `dist/` is gitignored, and the fix for a
stale site is to build it again.

The same rule is why `twin.js` is not in the tree. It was a 36 KB minified
emscripten bundle on a single line, committed: the worst possible merge
conflict. It is built from `twin/twin_glue.c` when emscripten is present, and
the twin page says so plainly when it is not.

**Nothing outside this repository.** The old pipeline searched machine-local
paths for a page generator that lived somewhere else, so a fresh clone could
not build the site at all, and CI built something no contributor could
reproduce. Everything needed is in this directory.

## Design system

`assets/design/` is vendored from the UltraWideLock v2 design system: mint on
deep teal, Space Grotesk and JetBrains Mono, dark canonical with a light theme
that holds AA. Token and class names are unchanged from that source, so it can
be re-vendored by copying files over.

Two signals, not one accent. Mint is the first path — direct, line-of-sight,
trusted. Amber is the late path — obstructed, or a relay's added delay. That is
the classifier the firmware actually ships, so the pair means the same thing on
the landing hero, in the twin and in the guides. The aliases are
`--path-first` and `--path-late` in `tokens/colors.css`.

Fonts are self-hosted WOFF2 in `fonts/`, declared by `tokens/typography.css`.
Nothing here reaches a third party. The one external subresource on the whole
site is `esp-web-tools` on the flasher page, pinned to an exact version with an
SRI hash and constrained by that page's CSP; the reasoning is in the comment
above the script tag.

The source files are split for authoring and concatenated into a single
`styles.css` at build time. Do not add an `@import` between them: each one is a
serial round trip the browser cannot discover until the parent sheet has
already arrived.
