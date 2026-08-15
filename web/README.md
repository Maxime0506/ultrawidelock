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

Fonts currently come from Google Fonts through an `@import` in
`tokens/typography.css`. That reaches a third party on every page load, which is
worth replacing with self-hosted WOFF2 before this site is public.
