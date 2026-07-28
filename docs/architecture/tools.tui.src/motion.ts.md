<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/motion.ts`

*No module docstring. First commit: "Give the bench TUI its labels back as border rules".*

**depends on** [`tools/tui/src/theme.ts`](theme.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](app.tsx.md)

## API

### `export function attachMotion(renderer: CliRenderer): () => void`
`tools/tui/src/motion.ts:13`

Start the animation clock.
Neither @opentui/core nor @opentui/solid ever attaches the timeline engine to
a renderer, so a registered timeline sits at frame zero until this runs.
Nothing throws when it is missing: the animations simply never move, which is
why the idle-frame test exists to notice.

**called by** `App`

### `export function createFade(from: RGBA, to: RGBA, duration = FADE_MS): Fade`
`tools/tui/src/motion.ts:35`

Ease between two theme colours.
The endpoints return the theme tokens themselves rather than a blend, so a
panel at rest is always the terminal's real palette colour; only the frames
in between use the approximation `mix` produces.

**called by** `App`  ·  **calls** `settle`

### `export function createPulse(duration = FADE_MS):`
`tools/tui/src/motion.ts:79`

A one-shot fade that starts already settled.
Reads 1 until something calls `restart()`, which drops it to 0 and eases back.
Starting settled is what makes it safe: with no animation clock the value stays
at 1 and every caller renders its resting colour, which is the screen the app
had before any of this existed.

**called by** `App`

### `export function createSpinner(active: Accessor<boolean>, period = 900): Accessor<string>`
`tools/tui/src/motion.ts:125`

A spinner that exists only while there is something to spin for.
Returns "" when `active()` is false, and registers no timer in that state, so
an idle workspace is completely still. The elapsed seconds come from the wall
clock rather than the frame count so a slow build still reports real time.

**called by** `App`  ·  **calls** `stop`

### `export function createEntrance(count: number, step = 60, duration = 220): Accessor<number>[]`
`tools/tui/src/motion.ts:173`

Stagger a fixed number of panels in on first draw, once.
Returns an accessor per index; each starts at 0 and ends at 1. Callers blend a
colour with it, so a run that never happens (no renderer attached, a frozen
test frame) leaves every panel at its resting colour and still readable.

**called by** `App`

### `export const titleCapacity = (width: number): number => width - 4`
`tools/tui/src/motion.ts:205`

The maximum border-title length a box of `width` columns will render.
OpenTUI drops an over-long title entirely rather than clipping it, so getting
this wrong loses the label with no other symptom. Measured against 0.4.5: a
box renders titles up to its width minus four.

**called by** `fitRule`

### `export function fitRule(parts: string[], width: number): string`
`tools/tui/src/motion.ts:215`

Fit ranked parts into a border rule, padded for the `╭─ label ─╮` shape.
Parts are dropped from the tail, so the caller's ordering decides what
survives on a narrow terminal. Returns "" when not even the first part fits,
because an empty rule and a dropped rule look the same and only one of them
is honest about it.

**called by** `App`, `CommandOutput`, `SerialTerminal`, `SidePane`, `WizardCard`  ·  **calls** `titleCapacity`

### `export function panelWidths(width: number, side: SideMode, compact: boolean): PanelWidths`
`tools/tui/src/motion.ts:250`

Every panel's rendered width, from one place.
Border titles are props, so they are fitted before layout runs and cannot ask
the renderer how wide their box turned out. This mirrors the flex rules in
app.tsx and rounds down at each step: underestimating drops one trailing hint,
while overestimating makes the whole label disappear.

**called by** `App`

### `export const panelColumns = (width: number): number => Math.max(0, width - 4)`
`tools/tui/src/motion.ts:259`

Content columns inside a panel of `width`: two for the border, two for the padding.

**called by** `App`

### `export const outputRows = (height: number): number => Math.max(5, Math.min(15, Math.round(height * 0.25)))`
`tools/tui/src/motion.ts:270`

The most rows the command output strip may take.
The strip and the console share the leftover space in a fixed ratio (see
OUTPUT_SHARE), which keeps the console dominant whatever else is open. This
caps the strip on a very tall window so the extra rows go to the console
rather than to a notice log nobody is reading, and floors it so a short one
still shows an error. Fifteen is about five times the prompt.

**called by** `App`

<details><summary>Undocumented (4)</summary>

- `towards`
- `restart` — tested: :a pulse starts settled so a frame with no animation clock is still correct@l83
- `stop`
- `read`

</details>
