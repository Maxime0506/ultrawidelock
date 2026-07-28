import { expect, test } from "bun:test"
import { BoxRenderable, engine, RGBA } from "@opentui/core"
import { createTestRenderer } from "@opentui/core/testing"
import { createRoot } from "solid-js"
import { createFade, createPulse, createSpinner, fitRule, panelColumns, panelWidths, titleCapacity } from "../src/motion"
import { settle, theme } from "../src/theme"

test("a border title longer than the box is dropped, and fitRule is what prevents it", async () => {
  // Pins the OpenTUI behaviour the whole chrome design depends on. If a future
  // version starts clipping instead of dropping, this test says so rather than
  // leaving a silently label-less panel to be noticed by eye.
  const width = 30
  const view = await createTestRenderer({ width: 40, height: 5 })
  const overlong = " ".repeat(1) + "x".repeat(titleCapacity(width) + 1) + " "
  view.renderer.root.add(
    new BoxRenderable(view.renderer, { border: true, title: overlong, borderColor: theme.line, width, height: 3 })
  )
  await view.renderOnce()
  expect(view.captureCharFrame()).not.toContain("xxx")
  view.renderer.destroy()

  const fitted = fitRule(["x".repeat(200)], width)
  expect(fitted).toBe("")
  const room = fitRule(["command output", "and a hint that will not fit alongside it"], width)
  expect(room.length).toBeLessThanOrEqual(titleCapacity(width))
  expect(room).toContain("command output")
})

test("fitRule drops whole entries from the tail and never overflows", () => {
  const parts = ["alpha", "beta", "gamma", "delta"]
  for (let width = 0; width <= 80; width++) {
    const line = fitRule(parts, width)
    expect(line.length).toBeLessThanOrEqual(Math.max(0, titleCapacity(width)))
    if (line === "") continue
    // Every surviving entry is intact and in rank order, never half a word.
    const kept = line.trim().split(" · ")
    expect(kept).toEqual(parts.slice(0, kept.length))
  }
  expect(fitRule([], 40)).toBe("")
  expect(fitRule(["", "beta"], 40)).toBe(" beta ")
})

test("panel widths account for the side pane and stay inside the terminal", () => {
  // The two primary panels plus the gap between them must never claim more than
  // the row they share, or a title fitted to the wider number disappears.
  for (const width of [60, 80, 108, 120, 200]) {
    for (const side of ["none", "half", "fixed"] as const) {
      const compact = width < 108
      const w = panelWidths(width, side, compact)
      expect(w.full).toBe(width - 2)
      // The console and an open side pane share one row with a gap between them.
      expect(w.serial + (side === "none" || compact ? 0 : w.side + 1)).toBeLessThanOrEqual(w.full)
      expect(w.output).toBe(w.full)
      expect(panelColumns(w.serial)).toBe(Math.max(0, w.serial - 4))
    }
  }
  expect(panelWidths(120, "none", false).side).toBe(0)
})

test("a fade rests on the theme token at both ends and blends only in between", () => {
  createRoot((dispose) => {
    const fade = createFade(theme.line, theme.foreground)
    expect(fade.color()).toBe(theme.line)

    fade.settle(true)
    engine.update(90)
    const middle = fade.color()
    expect(middle).not.toBe(theme.line)
    expect(middle).not.toBe(theme.foreground)

    engine.update(200)
    // Identity, not equality: a settled panel must be the terminal's real palette
    // colour, never the RGB approximation used while the fade is running.
    expect(fade.color()).toBe(theme.foreground)

    fade.settle(false)
    engine.update(300)
    expect(fade.color()).toBe(theme.line)
    dispose()
  })
})

test("a pulse starts settled so a frame with no animation clock is still correct", () => {
  createRoot((dispose) => {
    const pulse = createPulse(200)
    // This is what makes the motion safe to freeze: before anything animates, the
    // value already reads as finished and every caller paints its resting colour.
    expect(pulse.progress()).toBe(1)
    expect(settle(theme.foreground, theme.muted, pulse.progress())).toBe(theme.muted)

    pulse.restart()
    expect(pulse.progress()).toBe(0)
    expect(settle(theme.foreground, theme.muted, pulse.progress())).toBe(theme.foreground)

    engine.update(300)
    expect(pulse.progress()).toBe(1)
    dispose()
  })
})

// The spinner is driven by createEffect, which Solid defers until the root body
// has returned, so these read the accessor from outside it.
const withRoot = <T,>(build: () => T): [T, () => void] => {
  let value!: T
  const dispose = createRoot((stop) => {
    value = build()
    return stop
  })
  return [value, dispose]
}

test("the spinner is silent while nothing is running", () => {
  const [spinner, dispose] = withRoot(() => createSpinner(() => false))
  expect(spinner()).toBe("")
  // No timeline may have been registered, so time passing changes nothing.
  engine.update(5_000)
  expect(spinner()).toBe("")
  dispose()
})

test("the spinner turns and counts while a job runs, then goes away", () => {
  const [spinner, dispose] = withRoot(() => createSpinner(() => true))
  expect(spinner()).toMatch(/^[⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏] \d+s$/)

  const first = spinner()
  engine.update(400)
  expect(spinner()).not.toBe(first)

  // Disposal has to stop the timeline as well as blank the label, or a closed
  // workspace keeps a registered animation turning for the rest of the process.
  dispose()
  expect(spinner()).toBe("")
})

test("mixing colours never leaves the 0-255 range", () => {
  const blend = settle(theme.danger, theme.foreground, 0.5)
  for (const channel of blend.toInts()) {
    expect(channel).toBeGreaterThanOrEqual(0)
    expect(channel).toBeLessThanOrEqual(255)
  }
  expect(settle(theme.line, theme.foreground, -1)).toBe(theme.line)
  expect(settle(theme.line, theme.foreground, 2)).toBe(theme.foreground)
  expect(RGBA.fromInts(1, 2, 3, 255).toInts()).toEqual([1, 2, 3, 255])
})
