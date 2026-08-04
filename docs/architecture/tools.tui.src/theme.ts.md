<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/theme.ts`

**used by** [`tools/tui/src/app.tsx`](app.tsx.md), [`tools/tui/src/devices.ts`](devices.ts.md), [`tools/tui/src/motion.ts`](motion.ts.md)

## API

### `export function panelChrome(color: RGBA = theme.line)`
`tools/tui/src/theme.ts:37`

A panel's border and both of its title rules share one colour, because
OpenTUI has a single `titleColor` and no `bottomTitleColor`. That turns out to
be the right constraint: colour says what state the panel is in, and the label
and its hints brighten together when the panel is the one you are using.

**called by** `App`, `CommandOutput`, `SerialTerminal`, `SidePane`, `WizardCard`

### `export function mix(from: RGBA, to: RGBA, t: number): RGBA`
`tools/tui/src/theme.ts:54`

Blend two theme colours for an in-flight animation.
`toInts()` resolves an indexed or default colour to OpenTUI's own RGB
approximation of it, so a blended colour no longer defers to the terminal's
real palette. That is fine mid-fade and wrong at rest, which is why every
animation here ends by assigning the theme token rather than `mix(a, b, 1)`.

**called by** `settle`  ·  **calls** `at`

### `export const settle = (from: RGBA, to: RGBA, t: number): RGBA => (t <= 0 ? from : t >= 1 ? to : mix(from, to, t))`
`tools/tui/src/theme.ts:63`

`mix`, except the endpoints hand back the theme tokens so a settled frame is palette-true.

**called by** `App`, `colorFor`, `createFade`, `titleColor`  ·  **calls** `mix`

<details><summary>Undocumented (1)</summary>

- `at` — tested: :/test-request degrades to a named error when d1 is unreachable, rather than crashing@l179; :/test-request posts a pending container, pings only the awake owner, and records both candidates@l116; :/test-request with nobody registered posts nothing and says so@l99; :/test-result on a request with no i os version specified closes it but skips the matrix@l167; :/test-result outside any claim thread says so@l54; :/test-result pass: closes the request, records a validation, and edits the queue card@l88; :/test-result run by someone other than the claimer is refused@l69; :/test-result submitted twice: the second call reports already-recorded and does not double-write a validation@l145

</details>
