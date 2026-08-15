---
name: UltraWideLock
description: An instrument-grade dark interface for firmware that measures the distance instead of trusting it.
colors:
  ground: "#061012"
  surface: "#0a1618"
  card: "#0e1d1e"
  raise: "#152827"
  sunken: "#030a09"
  ink: "#c9d8d2"
  strong: "#f2faf6"
  muted: "#8da39b"
  faint: "#7b9188"
  inverse: "#04100c"
  line: "rgba(198,232,218,.145)"
  hairline: "rgba(198,232,218,.075)"
  line-strong: "rgba(198,232,218,.30)"
  accent: "#2ee6b8"
  accent-ink: "#5fecc4"
  accent-hover: "#8ff5d8"
  accent-tint: "rgba(46,230,184,.11)"
  accent-line: "rgba(46,230,184,.42)"
  ok: "#41c98a"
  warn: "#e0b341"
  danger: "#f2717a"
  focus: "#7ff0d4"
  selection: "rgba(46,230,184,.28)"
  path-first: "#2ee6b8"
  path-late: "#e0b341"
  refused: "#f2717a"
  scope: "#030a09"
typography:
  display:
    fontFamily: "Space Grotesk, Helvetica Neue, Helvetica, Segoe UI, system-ui, sans-serif"
    fontSize: "3.25rem"
    fontWeight: 700
    lineHeight: 1.06
    letterSpacing: "-0.024em"
  headline:
    fontFamily: "Space Grotesk, Helvetica Neue, Helvetica, Segoe UI, system-ui, sans-serif"
    fontSize: "2.375rem"
    fontWeight: 700
    lineHeight: 1.24
    letterSpacing: "-0.024em"
  title:
    fontFamily: "Space Grotesk, Helvetica Neue, Helvetica, Segoe UI, system-ui, sans-serif"
    fontSize: "1.75rem"
    fontWeight: 700
    lineHeight: 1.24
    letterSpacing: "-0.014em"
  body:
    fontFamily: "Space Grotesk, Helvetica Neue, Helvetica, Segoe UI, system-ui, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.68
    letterSpacing: "-0.006em"
  label:
    fontFamily: "Space Grotesk, Helvetica Neue, Helvetica, Segoe UI, system-ui, sans-serif"
    fontSize: "0.6875rem"
    fontWeight: 500
    lineHeight: 1.5
    letterSpacing: "0.09em"
  mono:
    fontFamily: "JetBrains Mono, ui-monospace, SF Mono, Cascadia Mono, Menlo, Consolas, monospace"
    fontSize: "0.8125rem"
    fontWeight: 400
    lineHeight: 1.6
    letterSpacing: "0"
    fontFeature: "tabular-nums"
rounded:
  xs: "2px"
  sm: "4px"
  md: "7px"
  lg: "11px"
  pill: "999px"
spacing:
  "1": "0.235rem"
  "2": "0.47rem"
  "3": "0.705rem"
  "4": "0.94rem"
  "5": "1.176rem"
  "6": "1.41rem"
  "8": "1.88rem"
  "10": "2.35rem"
  "12": "2.82rem"
  "16": "3.76rem"
  "20": "4.7rem"
  "28": "6.58rem"
components:
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.inverse}"
    rounded: "{rounded.md}"
    padding: "0.47rem 1.176rem"
    height: "2.35rem"
  button-primary-hover:
    backgroundColor: "{colors.accent-hover}"
    textColor: "{colors.inverse}"
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.strong}"
    rounded: "{rounded.md}"
    padding: "0.47rem 1.176rem"
    height: "2.35rem"
  button-secondary-hover:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.strong}"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.muted}"
    rounded: "{rounded.md}"
    padding: "0.47rem 1.176rem"
  button-sm:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.strong}"
    rounded: "{rounded.md}"
    padding: "0.235rem 0.705rem"
    height: "1.85rem"
  icon-button:
    backgroundColor: "transparent"
    textColor: "{colors.muted}"
    rounded: "{rounded.sm}"
    padding: "0"
    width: "2rem"
    height: "2rem"
  card:
    backgroundColor: "{colors.card}"
    textColor: "{colors.ink}"
    rounded: "{rounded.lg}"
    padding: "1.41rem"
  field:
    backgroundColor: "{colors.ground}"
    textColor: "{colors.strong}"
    rounded: "{rounded.md}"
    padding: "0.47rem 0.705rem"
  pill:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.muted}"
    typography: "{typography.mono}"
    rounded: "{rounded.pill}"
    padding: "0.235rem 0.705rem"
  pill-accent:
    backgroundColor: "{colors.accent-tint}"
    textColor: "{colors.accent-ink}"
    typography: "{typography.mono}"
    rounded: "{rounded.pill}"
    padding: "0.235rem 0.705rem"
  status-live:
    backgroundColor: "{colors.accent-tint}"
    textColor: "{colors.accent-ink}"
    typography: "{typography.mono}"
    rounded: "{rounded.pill}"
    padding: "0.235rem 0.705rem"
  navlink:
    backgroundColor: "transparent"
    textColor: "{colors.muted}"
    rounded: "{rounded.sm}"
    padding: "0.235rem 0.705rem"
  navlink-active:
    backgroundColor: "transparent"
    textColor: "{colors.strong}"
    rounded: "{rounded.sm}"
    padding: "0.235rem 0.705rem"
  callout-note:
    backgroundColor: "{colors.accent-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.md}"
    padding: "0.94rem 1.176rem"
  readout:
    backgroundColor: "{colors.card}"
    textColor: "{colors.strong}"
    typography: "{typography.mono}"
    rounded: "{rounded.sm}"
    padding: "0.705rem"
---

# Design System: UltraWideLock

## Overview

**Creative North Star: "Drawn To Scale"**

The firmware's whole claim is that it measures instead of trusting, and the
interface makes that same claim about itself. A hairline ruler runs down the
left edge of the landing page, ticked every 4rem, and everything to the right
of it inherits the posture: values are stated with their units, numbers are set
in tabular mono so they can be compared, the unlock bound is drawn as a labelled
decision line rather than as a gridline, and the scope band is a piece of
equipment set into the page rather than a picture of one. The ruler is not
decoration in the ornamental sense. It is decoration in the sense that a scale
on a drawing is decoration: it says the thing is to size.

The canvas is calm dark and alive at exactly one point. Five near-black
sea-teal surfaces carry the entire structure, held apart by 1px lines rather
than by shadow, and against that stillness a single mint accent marks whatever
is being measured right now: the wordmark's ranging dot, the live status pulse,
the trace on the scope, the fill of a meter, the inset edge of a selected row.
The restraint is what makes the accent legible as a signal instead of as
styling. There is no gradient, no glass, no decorative motion, and no second
brand colour competing for the same attention.

Precision extends to the material itself. The palette has no true grey in it:
every neutral is a teal held at very low chroma, and the text neutrals sit
almost exactly on the accent's own hue, so the greys read as the same material
as the signal rather than as a separate system laid underneath it. The surface
ramp rotates steadily toward that hue as it rises. Amber and red exist, but they
are not accents; they are verdicts, and they mean one specific thing each on
every surface of the site.

**Key Characteristics:**

- Measured: a ticked rail, stated units, tabular numerals, a labelled bound
- Calm dark canonical, with a light theme held to the same standard
- One accent, spent only on live measurement and current position
- Hairline structure; shadow reserved for things that leave the page plane
- Neutrals are the accent hue desaturated, never true grey
- Mono for anything that is an identifier, a number, or an instrument label

## Colors

A near-black sea-teal field with one mint signal in it, plus two verdict colours
that never do decorative work.

Dark is canonical and is what the frontmatter carries. The light theme is a full
peer, not a fallback: every token below has a light counterpart declared in the
same file, and both must land in the same commit. The light values live in
`.impeccable/design.json` under `colorMeta.<token>.light`.

### Primary

- **Ranging Mint** (`#2ee6b8`, `oklch(82.7% 0.154 170.5)`): the signal colour,
  lifted from the brand key art. It marks the thing that is measuring or the
  thing that is current, and nothing else. The wordmark's dot, the active nav
  underline, the meter fill, the scope trace, the progress bar, the step
  numerals, the pips, the inset edge on a selected row. Light theme drops to
  **#0e8a68** (`oklch(56.4% 0.112 167.4)`) because the same mint on white is not
  readable as text.
- **Mint Ink** (`#5fecc4`): the accent as *text*. Links, active table-of-contents
  entries, the eyebrow, the label inside an accent pill. Separate from the accent
  proper because a colour bright enough to be a marker is not always dark enough
  to be a foreground.
- **Mint Hover** (`#8ff5d8`): the one step brighter that a hover moves to.
  Light theme moves the other way, to **#085343**, since there brightening is
  the wrong direction for emphasis.

### Secondary

There is no secondary brand colour, and adding one would break the accent's
meaning. What looks like a second and third accent is the verdict pair below.

### Tertiary

- **Verdict Amber** (`#e0b341`, `oklch(78.7% 0.138 86.7)`): the late path.
  An obstructed measurement, or one carrying a relay's added delay. Also the
  colour a meter turns when it is reporting a constraint rather than a
  measurement, and the colour of the inline WebSerial warning.
- **Refusal Red** (`#f2717a`, `oklch(70.4% 0.159 17.8)`): a rejection. A refused
  unlock, a destructive action, a failed step.
- **Pass Green** (`#41c98a`, `oklch(74.7% 0.149 159.0)`): a completed action,
  distinct from the accent because "this finished" is not "this is live".

### Neutral

Nothing here is grey. Every neutral holds chroma between 0.010 and 0.028, and
the four text neutrals sit essentially on the accent's hue (164.9° to 171.6°
against its 170.5°). The five surfaces run cooler, 186.6° to 210.9°, and close
on the accent as they rise.

- **Ground** (`#061012`): the page itself, and the deepest thing a reader
  normally sees.
- **Surface** (`#0a1618`): chrome. The top bar, the sidebar, the footer, the
  command palette, the caption strip above a code block.
- **Card** (`#0e1d1e`): content sitting on the page. Cards, code blocks, tables,
  callouts, readouts.
- **Raise** (`#152827`): the hover step, and the track behind a progress bar.
  Never a resting state for a component.
- **Sunken** (`#030a09`): deeper than the page. Used for exactly one thing: the
  scope face, aliased as `--scope`, and the meter track.
- **Ink** (`#c9d8d2`) / **Strong** (`#f2faf6`) / **Muted** (`#8da39b`) /
  **Faint** (`#7b9188`): body text, headings and emphasis, secondary text and
  labels, and captions or units respectively.
- **Line** (`rgba(198,232,218,.145)`), **Hairline** (`.075`), **Line Strong**
  (`.30`): the structural vocabulary. Hairline separates things that belong
  together; line bounds a component; line-strong is a hover border or a
  separator that has to be seen.

### Named Rules

**The One Meaning Rule.** Mint is the first path: direct, line-of-sight,
trusted. Amber is the late path: obstructed, or carrying a relay's added delay.
Red is a refusal. That is the classifier the firmware actually ships, so a
colour cannot mean one thing on the landing hero and something else in the twin
or in a guide. This is product truth, not preference.

**The Alias Rule.** `--path-first`, `--path-late`, `--refused` and `--scope` are
declared exactly once, outside every theme block, because each only aliases a
primitive that is already themed. Add a meaning by adding an alias, never by
adding a third hex to two theme blocks that can then drift apart.

**The Rarity Rule.** The accent marks measurement and current position. If more
than a small fraction of a screen is mint, the accent has stopped being a signal
and the screen needs re-reading, not re-colouring.

**The 4.5 Rule.** Small text is checked against the surface it actually lands
on, not against the page. `landing.css` records `--faint` at 4.22:1 on the light
theme's scope face, which is under 4.5, and `--muted` at 5.53:1 there; the
diagram labels use `--muted` for that reason.

## Typography

**Display / Body Font:** Space Grotesk (with Helvetica Neue, Helvetica, Segoe
UI, system-ui, sans-serif)
**Label / Mono Font:** JetBrains Mono (with ui-monospace, SF Mono, Cascadia
Mono, Menlo, Consolas, monospace)

Both are self-hosted latin-subset variable WOFF2, 53,660 B for the pair
(`space-grotesk.woff2` 22,320 B, `jetbrains-mono.woff2` 31,340 B), under
SIL OFL 1.1. One file per family covers every weight the system asks for.

**Character:** Space Grotesk is a grotesque with drawn, slightly mechanical
details, so headings read as engineering rather than as marketing; the negative
tracking at display sizes tightens it further. JetBrains Mono does the technical
half of the work and carries real authority in the system: it is not only for
code but for every identifier, numeral, unit and instrument label, which is what
makes a readout look like a readout.

Root font-size is 17px, not the browser default of 16px, so every rem in the
type and space scales resolves against 17. Media-query rem does not: those
resolve against the initial 16px. A `64rem` breakpoint is 1024px while `64rem`
of layout width is 1088px, and that difference is deliberate rather than a bug.

### Hierarchy

- **Display** (700, `3.25rem` / `--fs-1000`, 1.06, -0.024em): the landing
  headline only, capped at 18ch and balanced. Drops to headline size below the
  64rem breakpoint.
- **Headline** (700, `2.375rem` / `--fs-900`, 1.24, -0.024em): the h1 of a guide
  or a tool page.
- **Title** (700, `1.75rem` / `--fs-800`, 1.24, -0.014em): h2. Carries a hairline
  rule above it and 3.76rem of air, so a section break is visible before it is
  read.
- **Subtitle** (700, `1.375rem` / `--fs-700`, 1.24, -0.014em): h3, and the value
  in a scope readout.
- **Lede** (400, `1.125rem` / `--fs-600`, 1.5, muted): the sentence under a
  headline, and the value in a dense readout. Capped at `--measure`.
- **Body** (400, `1rem` / `--fs-500`, 1.68, -0.006em): prose, capped at
  `--measure` (46rem) even when the column is wider.
- **UI** (400, `0.9375rem` / `--fs-400`): buttons, card copy, table cells, tabs.
- **Code** (400, `0.8125rem` / `--fs-300`, 1.6, mono): code blocks, sidebar
  entries, dense rows, inline code.
- **Label** (500, `0.6875rem` / `--fs-100`, 0.09em, uppercase): eyebrows, table
  headers, sidebar captions, meter keys, readout keys, footer captions. The
  smallest thing on the page, and never more than a few words.

The scale is hand-tuned rather than a fixed ratio: near-linear at UI sizes
(11, 12, 13, 15, 16px steps of a sixteenth of a rem each) and accelerating to
roughly 1.37× per step at display sizes.

### Named Rules

**The Tabular Rule.** Any number a reader might compare against another number
is set in JetBrains Mono with `font-variant-numeric: tabular-nums`. Stat blocks,
meter values, readouts, progress percentages, scope readings, and every cell in
a guide's tables of pins and registers. Numbers line up or they are not
comparable.

**The Caps Are Labels Rule.** Uppercase plus `--tr-caps` (0.09em) plus
`--fs-100` is the label voice and nothing else uses it. It names a value; it
never carries a sentence.

**The Mono Means Machine Rule.** Mono marks things the machine owns: symbols,
paths, constants, units, versions, states. Prose about those things stays in
Space Grotesk. If a mono string is a full sentence, it is in the wrong face.

## Layout

The site runs two shells. Documentation uses a fixed sidebar plus a fluid main
column: `--side` (17.5rem) alongside `minmax(0, 1fr)`, with the article itself a
`--measure` (46rem) column beside a `--toc` (14rem) rail, gapped by `--sp-12`.
Marketing and tool pages use a centred `--page-max` (80rem) container with
`--sp-8` of side padding. The top bar is `--topbar-h` (3.35rem), sticky, over a
`blur(12px)` backdrop at 88% surface, and every `[id]` heading carries
`scroll-margin-top` of that height plus `--sp-6` so an in-page link never lands
under it.

Spacing is a 4px grid expressed in rem against the 17px root: `--sp-1` is
0.235rem, which is 3.995px. Every step is a multiple of that, up to `--sp-28`
(6.58rem, 112px). Nothing in the system uses an off-grid gap.

The landing page adds `--rail` (2.75rem) of left gutter carrying the ruler:
a hairline at the edge and a 9px repeating tick every 4rem at 50% opacity. It
appears only above 64rem, where there is width to spare, and is dropped entirely
below that rather than being squeezed.

Breakpoints, all authored in rem against the initial 16px: **64rem** is the
main one (sidebar collapses, hero goes single-column, split and TOC collapse,
rail appears above it), **56rem** (footer to two columns, scope type scales up,
narrow-only content hidden), **52rem** (nav collapses into a sheet), **44rem**
(the ladder drops its middle column), **40rem** (rows stack, steps re-indent,
page padding tightens), **34rem** (footer to one column).

Below-fold sections marked `.defer-paint` use `content-visibility: auto` with
`contain-intrinsic-size: auto 32rem`, so the scrollbar stays honest before they
render.

### Named Rules

**The Measure Rule.** Prose stops at `--measure` (46rem) regardless of how wide
the column is. A wider container is a wider container, not a longer line.

**The Sideways Rule.** The page never scrolls horizontally. An element too wide
for the viewport gets its own scroll box and a sensible minimum: the scope keeps
`min-width: 38rem` inside `.scope-frame`, tables live in `.tablewrap`, tab
strips scroll their own row. Squeezing an instrument until it is illegible is
not responsive behaviour.

**The Drop It Rule.** When there is no room, remove rather than shrink. The rail
disappears below 64rem, `.hide-narrow` content disappears below 52rem, the row
gutter collapses below 40rem. Nothing hidden this way is the only route to a
piece of information.

## Elevation & Depth

Depth is carried by 1px lines and a five-step tonal ramp, not by shadow. The
ramp runs `--sunken` → `--ground` → `--surface` → `--card` → `--raise`, roughly
3% of OKLCH lightness per step (13.5%, 16.4%, 19.0%, 21.8%, then a wider 26.0%
for hover), and its hue rotates steadily toward the accent as it rises: 210.9°
at ground, 209.1° at surface, 201.2° at card, 191.5° at raise, against the
accent's 170.5°. Surfaces feel like they are made of the signal. `--sunken` sits
outside that rotation at 186.6°, because it is the scope face rather than a step
above the page.

Shadows exist but are reserved. `--shadow-1` is a seam, not an elevation.
`--shadow-inset` is a 3.5%-white top highlight that keeps a card from looking
like a hole. Real shadow appears only where something has genuinely left the
page plane.

### Shadow Vocabulary

- **Seam** (`--shadow-1`: `0 1px 1px rgba(0,0,0,.6)`): buttons and cards at
  rest. Enough to separate an edge from its background, not enough to read as
  lift.
- **Lifted** (`--shadow-2`: `0 1px 2px rgba(0,0,0,.6), 0 10px 24px -14px
  rgba(0,0,0,.75)`): the primary button, the mobile nav sheet, and a card under
  the cursor.
- **Floating** (`--shadow-3`: `0 2px 4px rgba(0,0,0,.55), 0 26px 60px -22px
  rgba(0,0,0,.85)`): the command palette, and nothing else currently.
- **Inset highlight** (`--shadow-inset`: `inset 0 1px 0 rgba(255,255,255,.035)`):
  cards and code blocks, paired with the seam.
- **Accent glow** (`--accent-glow`): a 1px accent ring plus a tight mint bloom.
  Reserved for the wordmark's dot and the scope's anchor. It marks a source of
  signal, not an elevated surface.

### Named Rules

**The On-Page / Off-Page Rule.** On the page: a line. Off the page: a shadow.
Nothing in between. If a surface is not floating over other content, it is
bounded by `--line` and separated by tone, and it gets at most the seam.

**The Hairline First Rule.** Reach for `--hairline` before `--line`, and for
`--line` before `--line-strong`. Hairline separates things that belong together
(list rows, footer sections, a sidebar from its content); line bounds a
component; line-strong is reserved for hover borders and for the one separator
that must be seen.

## Shapes

Rectilinear and tight. The radius scale is 2, 4, 7, 11, 16px, where each step
grows by one more pixel than the last, which keeps small components crisp while
still letting a card feel like an object. Assignment is by size and role rather
than by taste: `--r-xs` (2px) for inline code, kbd caps and signal pips;
`--r-sm` (4px) for icon buttons, list rows, search results and the focus ring's
own corner; `--r-md` (7px) for buttons, fields, code blocks and tables;
`--r-lg` (11px) for cards, the command palette and the ranging visualiser;
`--r-pill` for things genuinely capsule-shaped, which is status chips, pills,
progress tracks and step numerals.

Two shapes are deliberately asymmetric, and both are load-bearing. A callout is
`4px 7px 7px 4px`, tightening the left corners because a 3px accent rule runs
down that edge and a large radius there would leave a visible gap. A meter fill
is `0 4px 4px 0`, square at the near end so the bar visibly starts at the
baseline instead of floating.

Borders are 1px almost everywhere. `--bw-2` (2px) marks state rather than
structure: the active tab's underline, the active nav item's bar, the inset edge
of a selected row, the bottom of a kbd cap. `--bw-rule` (3px) is only the left
edge of a callout or a blockquote.

The recurring silhouette is the horizontal track: a scope band, a meter, a
progress bar, the ranging visualiser, the ladder. Things that measure are drawn
as a line with marks on it.

### Named Rules

**The Growing Step Rule.** Radii are 2, 4, 7, 11, 16. Do not interpolate a new
step; pick the one whose size matches the component.

**The Asymmetry Earns It Rule.** A non-uniform radius must be answering a
specific geometric problem, as the callout and the meter fill do. Otherwise
corners match.

## Components

Everything is restrained until addressed. At rest a control is a hairline border
and muted text; hover and focus are the first moment it commits colour, and it
commits fast, in `--dur-1` (80ms). Nothing pulses, glows or shifts to attract
attention it has not been given, with one exception: a live measurement is
allowed to move, because the movement is the information.

### Buttons

- **Shape:** gently tightened corners (`--r-md`, 7px), minimum height 2.35rem
  (40px), padding `--sp-2 --sp-5`.
- **Secondary (the default `.btn`):** surface background, strong text, 1px
  `--line` border, seam shadow. This is the ordinary button; primary is the
  exception.
- **Primary:** solid accent, `--inverse` text, accent border, lifted shadow.
  Hover moves to `--accent-hover`.
- **Ghost:** transparent with no border and muted text, filling to `--raise` on
  hover. For actions that must be available without being offered.
- **Danger:** `--danger-tint` background, `--danger-line` border, `--danger`
  text. Never solid red.
- **Hover / active:** 80ms background and border-colour transition; `:active`
  presses down 1px. Disabled drops to 45% opacity and `not-allowed`.
- **Small:** 1.85rem minimum height with code-size type, for toolbars and
  inline actions.
- **Icon button:** a 2rem square grid with `padding: 0` set explicitly, because
  a page's own bare `button { padding }` rule would otherwise win on that
  property and collapse the icon.

### Chips

- **Pill:** capsule, 1px `--line`, mono at `--fs-200`, muted on surface. Metadata
  that is a value rather than an action: a version, a board name, a licence.
- **Pill accent:** accent tint, accent line, mint ink. Marks the pill that is
  current or live.
- **Status:** a pill with a `0.42rem` dot before the label, coloured by state
  (idle faint, live mint, ok green, warn amber, error red). The live variant's
  dot pulses opacity on a 1.8s ease-in-out loop. It is the one always-on
  animation in the component set, and it earns it by meaning "this is happening
  now".

### Cards / Containers

- **Corner style:** 11px (`--r-lg`), the largest radius in normal use.
- **Background:** `--card`, one step above the page.
- **Shadow strategy:** seam plus inset highlight at rest. A linked card gains
  the lifted shadow, an `--accent-line` border and a 1px rise on hover, over
  `--dur-2` (150ms). A static card gains nothing, because it is not offering
  anything.
- **Border:** 1px `--line`.
- **Internal padding:** `--sp-6` (1.41rem); the feature-card variant uses
  `--sp-5`. Grids are `auto-fit` with a 15rem to 19rem minimum, gapped `--sp-4`.

### Inputs / Fields

- **Style:** `--ground` background sunk below the surface it sits on, 1px
  `--line`, 7px corners, `--fs-400`.
- **Focus:** the global ring, which is a 2px halo in the page colour (`--ground`
  in dark, `--surface` in light) followed by a 4px `--focus` ring, so it reads
  on any background without touching the element's own border. The command
  palette's search input is the exception:
  it suppresses the ring because the whole floating panel is already the focus.
- **Placeholder:** `--faint`.
- **Search button:** not an input at all. A full-width button styled like a
  field, with the shortcut key pushed to the right edge in a kbd cap.

### Navigation

- Top bar links are muted `--fs-400` at rest, going strong on `--raise` on
  hover. The current page is strong with a 2px accent underline drawn as an
  `::after` bar, not a border, so it can carry the pill radius.
- Below 52rem the links become a sheet under the bar, animated by `max-height`
  so no measurement step is needed, and the current item swaps its underline for
  an accent tint fill.
- The documentation sidebar is a sticky full-height `--surface` column with a
  scrollable tree. Selection is the inset marker, not a fill.
- Breadcrumbs are `--fs-300`, muted links, `--line-strong` separators, with the
  current item strong and ellipsised.

### Signature: the Instrument Set

The components that make this system itself rather than a generic dark theme.

- **Scope band:** a full-bleed `--sunken` band with a live SVG ranging round.
  One `data-state` attribute on the SVG root drives `--trace` and `--trace-line`
  for every mark inside it, so the instrument can never be half mint and half
  amber. Pulse animations are transform-only against a `--travel` variable set
  from the real geometry, so they run on the compositor with no per-frame
  JavaScript.
- **Readout (`.ro`):** a `--fs-100` uppercase key over a `--fs-600` mono tabular
  value. The densest way this system states a measurement.
- **Meter:** a labelled part-against-whole with both absolute values and the
  percentage, on a `--sunken` track. Fill is `--path-first` and turns
  `--path-late` past the point where it is reporting a constraint rather than a
  measurement. Not a chart, deliberately: two numbers do not earn axes.
- **Ranging ping / visualiser:** the identity animation. An impulse leaves the
  anchor, the first-path echo returns, rings decay. Pure CSS and no JavaScript,
  on a 28px grid: the inline ping loops at 2.8s with its second ring offset half
  a cycle, the full visualiser at 3.4s with three rings a third of a cycle
  apart.
- **Ladder:** the unlock sequence as a numbered three-column grid (index, mono
  step name, prose explanation), because the order is the content.
- **Steps:** a numbered procedure with a hairline spine and accent-ringed mono
  numerals, used where doing step three before step one would flash a board
  whose radio is not wired yet.

### Named Rules

**The Inset Marker Rule.** A selected row is marked by `inset 2px 0 0
var(--accent)` plus `--accent-tint`, never by a filled accent background. It
applies to sidebar items, search results and the mobile nav alike, so
"you are here" looks the same everywhere.

**The Motion Is Information Rule.** The site animates three things: a live
measurement, a state change, and an arrival. `--dur-1` (80ms) for a hover,
`--dur-2` (150ms) for a card or a copy button, `--dur-3` (240ms) for a panel,
`--dur-4` (420ms) for a scroll reveal, `--dur-5` (640ms) for a meter filling.
One rise distance (`--reveal-y`, 14px) and one 60ms stagger for every revealed
element on the site, so nothing arrives with its own idea of how far up is. Note
that the stagger is currently hardcoded as `i * 60` in `site.js` while
`--reveal-step` declares the same 60ms and is never read; they agree by
coincidence, not by wiring.

**The Reduced Motion Keeps The Instrument Rule.** Under
`prefers-reduced-motion`, the duration tokens collapse to 0ms and animations are
capped globally, but the diagram survives: the scope's three pulses park
mid-flight rather than vanishing, so it still shows what travels where.

## Do's and Don'ts

### Do:

- **Do** put every colour through a token. Outside its print block,
  `components.css` contains no literal colour at all, and the print block is the
  one deliberate exception: it overrides the tokens with plain ink-on-paper
  values because paper has no theme. A handful of literal dimensions survive
  where no token would mean anything (a 9px scrollbar, a 2px pip gap, a 28px
  diagram grid, animation loop lengths); colour is not one of them.
- **Do** ship both theme values in the same commit. Dark is canonical; light is
  a peer that has to hold up, and a token with only one half declared is a bug.
- **Do** check small text against the surface it actually lands on. Use
  `--muted` over `--faint` on the scope face, where `--faint` measures 4.22:1.
- **Do** mark a selection with the inset rule (`inset 2px 0 0 var(--accent)`
  plus `--accent-tint`), on every list in the system.
- **Do** give a too-wide element its own scroll box with a sensible minimum,
  and let the page itself stay put.

### Don't:

- **Don't** add an `@import` between the source sheets. `build.py` concatenates
  `CSS_PARTS` in authoring order into a single stylesheet; an `@import`
  re-creates the serial fetch chain in front of first paint that the build was
  written to remove.
- **Don't** load a third-party font or stylesheet. Both faces are self-hosted
  latin-subset variable WOFF2 under SIL OFL 1.1, which is a privacy decision on
  a lock project's site as much as a performance one.
- **Don't** let a page redeclare a component the design system already owns. The
  twin once carried its own top bar, wordmark and theme toggle, which won every
  conflict on source order including the ones it did not intend to. Page-scoped
  CSS aliases tokens; it does not restyle shared components.
- **Don't** read tokens from a canvas or WebGL surface every frame. Those
  surfaces map design tokens to their own adapter names once and listen for the
  `uwl:theme` event, because `getComputedStyle` at 60fps is a forced style
  recalculation.
- **Don't** detect an external link with `[href^="http"]`. The marker is a
  build-time `data-ext` attribute, so an in-page anchor to an absolute URL on
  our own host is not mislabelled.
- **Don't** invent a hex for a state that already has a semantic alias. Mint,
  amber and red are spoken for; a new state gets a new alias over an existing
  primitive.
- **Don't** add a token before checking the seven that are already declared and
  unreferenced: `--fs-1100`, `--article`, `--r-xl`, `--sp-40`, `--ease-spring`,
  `--ease-decel`, `--reveal-step`.
- **Don't** animate anything the global reduced-motion cap cannot stop, and
  don't let that cap leave a diagram blank; park it in a readable state instead.
