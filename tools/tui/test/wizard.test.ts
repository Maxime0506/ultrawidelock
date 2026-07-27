import { expect, test } from "bun:test"
import type { TargetSnapshot } from "../src/targets"
import { destructiveActions, wizardBackAction, wizardView, type WizardContext } from "../src/wizard"

const snapshot = (values: Partial<TargetSnapshot> = {}): TargetSnapshot => ({
  target: "nrf",
  setupReady: false,
  setupDetail: "setup missing",
  artifact: "missing",
  artifactPath: "build/merged.hex",
  compatiblePorts: [],
  incompatiblePorts: [],
  ...values
})

const context = (values: Partial<WizardContext> = {}): WizardContext => ({
  target: "nrf",
  snapshot: snapshot(),
  connection: "disconnected",
  pairingReady: false,
  ...values
})

const actions = (view: ReturnType<typeof wizardView>) => view.choices.map(({ value }) => value)

test("offers bootstrap and hardware-free progress when an nRF clone is not ready", () => {
  const view = wizardView("home", context())
  expect(actions(view)).toContain("bootstrap-confirm")
  expect(actions(view)).toContain("test")
  expect(actions(view)).not.toContain("flash-choice")
})

test("offers build, port selection, flash, and pairing only when state supports them", () => {
  const ready = snapshot({
    setupReady: true,
    setupDetail: "ready",
    artifact: "current",
    compatiblePorts: ["/dev/board-a", "/dev/board-b"]
  })
  const view = wizardView("home", context({ snapshot: ready }))
  expect(actions(view)).toContain("build-choice")
  expect(actions(view)).toContain("choose-port")
  expect(actions(view)).toContain("flash-choice")
  expect(actions(view)).toContain("pair")
})

test("labels the recommended nRF console separately from its silent alternate interface", () => {
  const view = wizardView("choose-port", context({
    snapshot: snapshot({
      compatiblePorts: ["/dev/cu.usbmodem0003", "/dev/cu.usbmodem0001"]
    })
  }))

  expect(view.choices[0]?.name).toContain("console")
  expect(view.choices[0]?.description).toContain("recommended VCOM1")
  expect(view.choices[1]?.description).toContain("alternate J-Link interface")
})

test("does not offer guided flashing from an artifact older than repository source", () => {
  const stale = snapshot({
    setupReady: true,
    artifact: "older-than-source",
    compatiblePorts: ["/dev/board"]
  })
  const view = wizardView("home", context({ snapshot: stale }))
  expect(actions(view)).toContain("build-choice")
  expect(actions(view)).not.toContain("flash-choice")
})

test("does not invent pairing or automatic toolchain installation for the standalone ESP reader", () => {
  const readerSnapshot = snapshot({
    target: "esp32-reader",
    setupReady: false,
    artifactPath: "ports/esp32/apps/reader/build/woz_uwb_esp32s3.bin"
  })
  const view = wizardView("home", context({ target: "esp32-reader", snapshot: readerSnapshot }))
  expect(actions(view)).not.toContain("pair")
  expect(actions(view)).not.toContain("bootstrap-confirm")
  expect(actions(view)).toContain("pane:overview")
})

test("no destructive action is reachable from a menu without passing through a confirmation", () => {
  const ready = snapshot({ setupReady: true, artifact: "current", compatiblePorts: ["/dev/board"] })
  const reachable = new Set<string>()
  for (const stage of ["home", "build-choice", "flash-choice", "diagnostics", "recovery", "pair", "choose-port"] as const) {
    for (const value of actions(wizardView(stage, context({ snapshot: ready, connection: "connected" })))) reachable.add(value)
  }
  for (const action of destructiveActions) {
    expect(reachable).not.toContain(action)
    expect(reachable).toContain(`confirm:${action}`)
  }
})

test("every destructive confirmation offers the same escape-first shape", () => {
  for (const action of destructiveActions) {
    const view = wizardView("confirm", context({ pending: action }))
    expect(view.eyebrow).toBe("destructive confirmation")
    expect(view.danger).toBe(true)
    // Declining is always first, so the default highlight can never be the accept.
    expect(view.choices[0]?.name).toBe("Go back")
    expect(view.choices[0]?.danger).not.toBe(true)
    expect(view.choices).toHaveLength(2)
    expect(view.choices[1]).toMatchObject({ value: action, danger: true })
  }
})

test("each confirmation names the loss that is specific to it", () => {
  expect(wizardView("confirm", context({ pending: "flash" })).detail).toContain("preserved")
  expect(wizardView("confirm", context({ pending: "flash-erase" })).detail).toContain("trusted credentials")
  expect(wizardView("confirm", context({ pending: "rebuild-flash-erase" })).detail).toContain("build directory")
  expect(wizardView("confirm", context({ pending: "factory-reset" })).detail).toContain("trusted phone key")
})

test("factory reset is offered only on targets whose firmware has the command", () => {
  const connected = context({ connection: "connected", snapshot: snapshot({ setupReady: true }) })
  expect(actions(wizardView("diagnostics", connected))).toContain("confirm:factory-reset")
  expect(
    actions(wizardView("diagnostics", { ...connected, target: "esp32-reader", snapshot: snapshot({ target: "esp32-reader" }) }))
  ).not.toContain("confirm:factory-reset")
})

test("failed work returns to recoverable actions instead of a dead end", () => {
  const view = wizardView("recovery", context({ recovery: "build failed" }))
  expect(actions(view)).toContain("home")
  expect(actions(view)).toContain("test")
  expect(actions(view)).toContain("pane:jobs")
})

test("active work overrides every stale option screen", () => {
  for (const jobState of ["queued", "running"] as const) {
    for (const stage of ["home", "build-choice", "flash-choice", "diagnostics", "choose-target"] as const) {
      const view = wizardView(stage, context({ jobState }))
      expect(view.eyebrow).toBe("working")
      expect(actions(view)).toEqual(["pane:jobs", "cancel-jobs", "command-mode"])
      expect(actions(view)).not.toContain("build")
      expect(actions(view)).not.toContain("rebuild")
    }
  }
})

test("pending inventory replaces stale choices with responsive navigation", () => {
  const view = wizardView("home", context({ inventoryPending: true }))
  expect(view.eyebrow).toBe("checking bench")
  expect(view.title).toContain("Finding nRF5340 DK")
  expect(actions(view)).toEqual(["target-menu", "command-mode"])
  expect(actions(view)).not.toContain("connect")
  expect(actions(view)).not.toContain("build")
})

test("left-arrow back navigation follows the wizard hierarchy without escaping active work", () => {
  expect(wizardBackAction("choose-port", context())).toBe("home")
  expect(wizardBackAction("confirm", context({ pending: "flash-erase" }))).toBe("flash-choice")
  expect(wizardBackAction("confirm", context({ pending: "factory-reset" }))).toBe("diagnostics")
  expect(wizardBackAction("home", context())).toBe("target-menu")
  expect(wizardBackAction("choose-target", context())).toBeUndefined()
  expect(wizardBackAction("choose-port", context({ jobState: "running" }))).toBeUndefined()
})
