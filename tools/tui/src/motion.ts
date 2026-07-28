import { createTimeline, engine, type CliRenderer, type RGBA, type Timeline } from "@opentui/core"
import { createEffect, createSignal, onCleanup, type Accessor } from "solid-js"
import { settle } from "./theme"

/**
 * Start the animation clock.
 *
 * Neither @opentui/core nor @opentui/solid ever attaches the timeline engine to
 * a renderer, so a registered timeline sits at frame zero until this runs.
 * Nothing throws when it is missing: the animations simply never move, which is
 * why the idle-frame test exists to notice.
 */
export function attachMotion(renderer: CliRenderer): () => void {
  engine.attach(renderer)
  return () => engine.detach()
}

/** How long a colour transition takes. Long enough to read as motion, short enough to never be in the way. */
const FADE_MS = 180

export type Fade = {
  /** The colour to paint this frame. */
  color: Accessor<RGBA>
  /** Ease towards `to` when true, back to `from` when false. */
  settle: (active: boolean) => void
}

/**
 * Ease between two theme colours.
 *
 * The endpoints return the theme tokens themselves rather than a blend, so a
 * panel at rest is always the terminal's real palette colour; only the frames
 * in between use the approximation `mix` produces.
 */
export function createFade(from: RGBA, to: RGBA, duration = FADE_MS): Fade {
  const [progress, setProgress] = createSignal(0)
  const state = { p: 0 }
  let timeline: Timeline | undefined

  const stop = () => {
    if (!timeline) return
    timeline.pause()
    engine.unregister(timeline)
    timeline = undefined
  }

  const towards = (active: boolean) => {
    const goal = active ? 1 : 0
    if (state.p === goal) return
    stop()
    const next = createTimeline({ autoplay: true })
    next.add(state, {
      p: goal,
      duration,
      ease: "outQuad",
      onUpdate: () => setProgress(state.p),
      onComplete: () => {
        state.p = goal
        setProgress(goal)
        stop()
      }
    })
    engine.register(next)
    timeline = next
  }

  onCleanup(stop)
  return { color: () => settle(from, to, progress()), settle: towards }
}

/**
 * A one-shot fade that starts already settled.
 *
 * Reads 1 until something calls `restart()`, which drops it to 0 and eases back.
 * Starting settled is what makes it safe: with no animation clock the value stays
 * at 1 and every caller renders its resting colour, which is the screen the app
 * had before any of this existed.
 */
export function createPulse(duration = FADE_MS): { progress: Accessor<number>; restart: () => void } {
  const [progress, setProgress] = createSignal(1)
  const state = { p: 1 }
  let timeline: Timeline | undefined

  const stop = () => {
    if (!timeline) return
    timeline.pause()
    engine.unregister(timeline)
    timeline = undefined
  }

  const restart = () => {
    stop()
    state.p = 0
    setProgress(0)
    const next = createTimeline({ autoplay: true })
    next.add(state, {
      p: 1,
      duration,
      ease: "outQuad",
      onUpdate: () => setProgress(state.p),
      onComplete: () => {
        state.p = 1
        setProgress(1)
        stop()
      }
    })
    engine.register(next)
    timeline = next
  }

  onCleanup(stop)
  return { progress, restart }
}

/** Braille dots read as motion at any font size and stay inside one cell. */
export const spinnerFrames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]

/**
 * A spinner that exists only while there is something to spin for.
 *
 * Returns "" when `active()` is false, and registers no timer in that state, so
 * an idle workspace is completely still. The elapsed seconds come from the wall
 * clock rather than the frame count so a slow build still reports real time.
 */
export function createSpinner(active: Accessor<boolean>, period = 900): Accessor<string> {
  const [label, setLabel] = createSignal("")
  let timeline: Timeline | undefined

  const stop = () => {
    if (timeline) {
      timeline.pause()
      engine.unregister(timeline)
      timeline = undefined
    }
    setLabel("")
  }

  createEffect(() => {
    if (!active()) {
      stop()
      return
    }
    if (timeline) return
    const startedAt = Date.now()
    const state = { turn: 0 }
    const next = createTimeline({ autoplay: true })
    next.add(state, {
      turn: spinnerFrames.length,
      duration: period,
      ease: "linear",
      loop: true,
      onUpdate: () => {
        const frame = spinnerFrames[Math.floor(state.turn) % spinnerFrames.length]
        setLabel(`${frame} ${Math.floor((Date.now() - startedAt) / 1000)}s`)
      }
    })
    engine.register(next)
    timeline = next
    setLabel(`${spinnerFrames[0]} 0s`)
  })

  onCleanup(stop)
  return label
}

/**
 * Stagger a fixed number of panels in on first draw, once.
 *
 * Returns an accessor per index; each starts at 0 and ends at 1. Callers blend a
 * colour with it, so a run that never happens (no renderer attached, a frozen
 * test frame) leaves every panel at its resting colour and still readable.
 */
export function createEntrance(count: number, step = 60, duration = 220): Accessor<number>[] {
  const fades = Array.from({ length: count }, () => createSignal(0))
  const state = Array.from({ length: count }, () => ({ p: 0 }))
  const timeline = createTimeline({ autoplay: true })
  for (let index = 0; index < count; index++) {
    timeline.add(
      state[index],
      {
        p: 1,
        duration,
        ease: "outQuad",
        onUpdate: () => fades[index][1](state[index].p),
        onComplete: () => fades[index][1](1)
      },
      index * step
    )
  }
  engine.register(timeline)
  onCleanup(() => {
    timeline.pause()
    engine.unregister(timeline)
  })
  return fades.map(([read]) => read)
}

/**
 * The maximum border-title length a box of `width` columns will render.
 *
 * OpenTUI drops an over-long title entirely rather than clipping it, so getting
 * this wrong loses the label with no other symptom. Measured against 0.4.5: a
 * box renders titles up to its width minus four.
 */
export const titleCapacity = (width: number): number => width - 4

/**
 * Fit ranked parts into a border rule, padded for the `╭─ label ─╮` shape.
 *
 * Parts are dropped from the tail, so the caller's ordering decides what
 * survives on a narrow terminal. Returns "" when not even the first part fits,
 * because an empty rule and a dropped rule look the same and only one of them
 * is honest about it.
 */
export function fitRule(parts: string[], width: number): string {
  const room = titleCapacity(width) - 2
  if (room <= 0) return ""
  let line = ""
  for (const part of parts) {
    if (!part) continue
    const next = line === "" ? part : `${line} · ${part}`
    if (next.length > room) break
    line = next
  }
  return line === "" ? "" : ` ${line} `
}

/** How the side pane is sized: closed, half the row (the jobs pane), or its fixed column. */
export type SideMode = "none" | "half" | "fixed"

export type PanelWidths = {
  /** Full inner width, used by the wizard card and the prompt. */
  full: number
  /** The command output column. */
  output: number
  /** The serial terminal column. */
  serial: number
  /** The side pane, 0 when it is closed. */
  side: number
}

/**
 * Every panel's rendered width, from one place.
 *
 * Border titles are props, so they are fitted before layout runs and cannot ask
 * the renderer how wide their box turned out. This mirrors the flex rules in
 * app.tsx and rounds down at each step: underestimating drops one trailing hint,
 * while overestimating makes the whole label disappear.
 */
export function panelWidths(width: number, side: SideMode, compact: boolean, stacked: boolean): PanelWidths {
  const full = Math.max(0, width - 2)
  const sideWidth =
    side === "none" ? 0 : compact ? full : side === "half" ? Math.floor(full / 2) : Math.min(42, full)
  const primary = side === "none" || compact ? full : Math.max(0, full - sideWidth - 1)
  if (stacked) return { full, output: primary, serial: primary, side: sideWidth }
  const output = Math.floor(primary * 0.58)
  return { full, output, serial: Math.max(0, primary - output - 1), side: sideWidth }
}

/** Content columns inside a panel of `width`: two for the border, two for the padding. */
export const panelColumns = (width: number): number => Math.max(0, width - 4)
