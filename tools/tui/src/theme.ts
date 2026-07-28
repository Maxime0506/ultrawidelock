import { RGBA, TextAttributes } from "@opentui/core"

// Use the terminal's own default foreground, background, and ANSI palette.
// This deliberately avoids imposing an application colour scheme on the user.
export const theme = {
  foreground: RGBA.defaultForeground(),
  background: RGBA.defaultBackground(),
  line: RGBA.fromIndex(8),
  muted: RGBA.fromIndex(8),
  nrf: RGBA.defaultForeground(),
  esp32: RGBA.defaultForeground(),
  success: RGBA.fromIndex(2),
  warning: RGBA.fromIndex(3),
  danger: RGBA.fromIndex(1)
} as const

export const severityColor = {
  info: theme.foreground,
  success: theme.success,
  warning: theme.warning,
  error: theme.danger
} as const

// Hierarchy comes from weight, not from colour, so the screen keeps working on
// whatever palette the user runs. Bold marks the one thing that matters in a
// region; dim marks everything supporting it.
export const attrs = {
  strong: TextAttributes.BOLD,
  subtle: TextAttributes.DIM,
  none: TextAttributes.NONE
} as const

// A panel's border and both of its title rules share one colour, because
// OpenTUI has a single `titleColor` and no `bottomTitleColor`. That turns out to
// be the right constraint: colour says what state the panel is in, and the label
// and its hints brighten together when the panel is the one you are using.
export function panelChrome(color: RGBA = theme.line) {
  return {
    border: true,
    borderStyle: "rounded",
    borderColor: color,
    titleColor: color
  } as const
}

/**
 * Blend two theme colours for an in-flight animation.
 *
 * `toInts()` resolves an indexed or default colour to OpenTUI's own RGB
 * approximation of it, so a blended colour no longer defers to the terminal's
 * real palette. That is fine mid-fade and wrong at rest, which is why every
 * animation here ends by assigning the theme token rather than `mix(a, b, 1)`.
 */
export function mix(from: RGBA, to: RGBA, t: number): RGBA {
  const clamped = t < 0 ? 0 : t > 1 ? 1 : t
  const [fr, fg, fb] = from.toInts()
  const [tr, tg, tb] = to.toInts()
  const at = (a: number, b: number) => Math.round(a + (b - a) * clamped)
  return RGBA.fromInts(at(fr, tr), at(fg, tg), at(fb, tb), 255)
}

/** `mix`, except the endpoints hand back the theme tokens so a settled frame is palette-true. */
export const settle = (from: RGBA, to: RGBA, t: number): RGBA => (t <= 0 ? from : t >= 1 ? to : mix(from, to, t))
