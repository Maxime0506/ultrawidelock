import { afterEach, expect, test } from "bun:test"
import { testRender } from "@opentui/solid"
import { App, CommandOutput, SerialTerminal, SidePane, WizardCard } from "../src/app"
import { makeBoardState } from "../src/devices"
import type { Job } from "../src/types"

let teardown: (() => void) | undefined
const noPorts = async () => []

afterEach(() => teardown?.())

test("renders a terminal-native command workspace at standard and compact terminal widths", async () => {
  const wide = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 120, height: 36 })
  teardown = () => wide.renderer.destroy()
  await wide.renderOnce()
  expect(wide.captureCharFrame()).toContain("openaliro")
  expect(wide.captureCharFrame()).toContain("command output")
  expect(wide.captureCharFrame()).toContain("serial terminal")
  expect(wide.captureCharFrame()).toContain("No serial session.")
  expect(wide.captureCharFrame()).toContain("connect, wizard, or ? for help")
  expect(wide.captureCharFrame()).toContain("What do you want to put on the bench?")
  expect(wide.captureCharFrame()).toContain("Tab command")
  expect(wide.captureCharFrame()).toContain("wizard on|off")
  expect(wide.captureCharFrame()).toContain("pane on|off")
  wide.renderer.destroy()

  const compact = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 70, height: 30 })
  teardown = () => compact.renderer.destroy()
  await compact.renderOnce()
  expect(compact.captureCharFrame()).toContain("command output")
  expect(compact.captureCharFrame()).toContain("serial terminal")
  expect(compact.captureCharFrame()).toContain("guided setup")
})

test("moves from wizard choices to the expert prompt and renders complete help", async () => {
  const view = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 120, height: 42 })
  teardown = () => view.renderer.destroy()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressTab()
  await view.waitForFrame((candidate) => candidate.includes("TUI command mode") && !candidate.includes("Tab to type"))
  await view.mockInput.typeText("?")
  view.mockInput.pressEnter()
  const top = await view.waitForFrame((candidate) => candidate.includes("command reference"))
  let pagedFrames = top
  for (let index = 0; index < 8; index++) {
    view.mockInput.pressKey("\u001b[6~")
    await new Promise((resolve) => setTimeout(resolve, 5))
    await view.renderOnce()
    pagedFrames += view.captureCharFrame()
  }
  expect(pagedFrames).toContain("target esp32-lock")
  expect(pagedFrames).toContain("capture start|stop|clear")
  expect(pagedFrames).toContain("PageUp / PageDown")
  expect(pagedFrames).toContain("Hide the wizard")
}, 30_000)

test("keeps output, wizard, and prompt separate at 80x24", async () => {
  const view = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 80, height: 24 })
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  const frame = view.captureCharFrame()
  const lines = frame.split("\n")
  const output = lines.findIndex((line) => line.includes("command output"))
  const terminal = lines.findIndex((line) => line.includes("serial terminal"))
  const wizard = lines.findIndex((line) => line.includes("What do you want to put on the bench?"))
  const prompt = lines.findIndex((line) => line.includes("connect, wizard, or ? for help"))
  expect(output).toBeGreaterThan(0)
  expect(terminal).toBeGreaterThan(output)
  expect(wizard).toBeGreaterThan(terminal)
  expect(prompt).toBeGreaterThan(wizard)
  expect(frame).not.toContain("wizard─")
})

test("shows the left-arrow shortcut when a wizard screen has a parent", async () => {
  const view = await testRender(
    () => (
      <WizardCard
        view={{
          eyebrow: "serial device",
          title: "Which attached board should this session use?",
          detail: "Choose a compatible console.",
          choices: [{ name: "Go back", description: "Return to setup.", value: "home" }]
        }}
        focused
        short={false}
        pulse="·"
        canGoBack
        onReady={() => undefined}
        onAction={() => undefined}
      />
    ),
    { width: 100, height: 12 }
  )
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  expect(view.captureCharFrame()).toContain("q close · ← back · ↑↓ choose · Enter · Tab command")
})

test("uses Left Arrow to return to the previous wizard screen", async () => {
  const view = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 100, height: 34 })
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressEnter()
  await view.waitForFrame((candidate) => candidate.includes("is ready for the next step"))
  view.mockInput.pressArrow("left")
  await view.waitForFrame((candidate) => candidate.includes("What do you want to put on the bench?"))
}, 30_000)

test("q hides only the focused wizard and transfers focus to the prompt", async () => {
  const view = await testRender(
    () => <App autoConnect={false} discoverPorts={noPorts} />,
    { width: 100, height: 34 }
  )
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  view.mockInput.pressKey("q")
  const hidden = await view.waitForFrame(
    (candidate) => !candidate.includes("What do you want to put on the bench?") && candidate.includes("TUI command mode")
  )
  expect(hidden).toContain("TUI command mode")
  await view.mockInput.typeText("q")
  await view.waitForFrame((candidate) => candidate.includes("> q"))
}, 30_000)

test("renders an interactive checking state while serial inventory is pending", async () => {
  let finishInventory: (ports: []) => void = () => undefined
  const inventory = new Promise<[]>((resolve) => {
    finishInventory = resolve
  })
  const view = await testRender(
    () => <App autoConnect={false} discoverPorts={() => inventory} />,
    { width: 100, height: 34 }
  )
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressEnter()
  const checking = await view.waitForFrame((candidate) => candidate.includes("Finding nRF5340 DK"))
  expect(checking).toContain("Use the command line")
  expect(checking).toContain("Back to target selection")
  finishInventory([])
  await view.waitForFrame((candidate) => candidate.includes("is ready for the next step"))
}, 30_000)

test("wizard off yields its space and wizard on restores the guide", async () => {
  const view = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 120, height: 42 })
  teardown = () => view.renderer.destroy()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressTab()
  await view.waitForFrame((candidate) => candidate.includes("TUI command mode"))

  await view.mockInput.typeText("wizard off")
  view.mockInput.pressEnter()
  await view.waitForFrame(
    (candidate) => !candidate.includes("What do you want to put on the bench?") && candidate.includes("wizard on|off")
  )

  await view.mockInput.typeText("wizard on")
  view.mockInput.pressEnter()
  await view.waitForFrame(
    (candidate) => candidate.includes("What do you want to put on the bench?") && candidate.includes("Tab to type")
  )
}, 30_000)

test("plain clear empties TUI output instead of being forwarded to firmware", async () => {
  const view = await testRender(
    () => <App autoConnect={false} discoverPorts={noPorts} />,
    { width: 120, height: 36 }
  )
  teardown = () => view.renderer.destroy()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressTab()
  await view.waitForFrame((candidate) => candidate.includes("TUI command mode"))
  await view.mockInput.typeText("clear")
  view.mockInput.pressEnter()
  const cleared = await view.waitForFrame((candidate) => candidate.includes("No TUI activity yet."))
  expect(cleared).not.toContain("> clear")
  expect(cleared).not.toContain("is not connected")
}, 30_000)

test("pane on restores the most recently selected side pane", async () => {
  const view = await testRender(() => <App autoConnect={false} discoverPorts={noPorts} />, { width: 120, height: 42 })
  teardown = () => view.renderer.destroy()
  await new Promise((resolve) => setTimeout(resolve, 10))
  view.mockInput.pressTab()
  await view.waitForFrame((candidate) => candidate.includes("TUI command mode"))

  await view.mockInput.typeText("pane jobs")
  view.mockInput.pressEnter()
  await view.waitForFrame((candidate) => candidate.includes("No jobs in this session."))

  await view.mockInput.typeText("pane off")
  view.mockInput.pressEnter()
  const closed = await view.waitForFrame((candidate) => !candidate.includes("No jobs in this session."))
  expect(closed.split("\n").find((line) => line.includes("command output"))).toContain("Shift+PageUp/PageDown")

  await view.mockInput.typeText("pane on")
  view.mockInput.pressEnter()
  await view.waitForFrame((candidate) => candidate.includes("No jobs in this session."))
}, 30_000)

test("keeps command, serial, and job output in separate panes", async () => {
  const board = makeBoardState("nrf")
  const job: Job = {
    id: "build",
    label: "nRF build",
    command: ["make", "build"],
    cwd: ".",
    state: "running",
    output: ["compile-only-sentinel", "linking firmware"]
  }

  board.connection = "connected"
  board.port = "/dev/example"
  const commands = await testRender(
    () => (
      <CommandOutput
        activity={[{ timestamp: 0, kind: "message", severity: "info", text: "command-only-sentinel" }]}
        showHelp
        onReady={() => undefined}
      />
    ),
    { width: 76, height: 18 }
  )
  teardown = () => commands.renderer.destroy()
  await commands.renderOnce()
  expect(commands.captureCharFrame()).toContain("command output")
  expect(commands.captureCharFrame()).toContain("command-only-sentinel")
  expect(commands.captureCharFrame()).toContain("command reference")
  expect(commands.captureCharFrame()).not.toContain("serial-only-sentinel")
  expect(commands.captureCharFrame()).not.toContain("compile-only-sentinel")
  commands.renderer.destroy()

  board.pairing = { qrContent: "MT:Y.K9042C00KA0648G00", manualCode: "34970112332" }
  const terminal = await testRender(
    () => (
      <SerialTerminal
        board={board}
        serialLines={["openaliro> ", "serial-only-sentinel"]}
        onReady={() => undefined}
      />
    ),
    { width: 76, height: 18 }
  )
  teardown = () => terminal.renderer.destroy()
  await terminal.renderOnce()
  expect(terminal.captureCharFrame()).toContain("serial terminal")
  expect(terminal.captureCharFrame()).toContain("live · /dev/example · 115200 baud · VT")
  expect(terminal.captureCharFrame()).toContain("serial-only-sentinel")
  expect(terminal.captureCharFrame()).not.toContain("command-only-sentinel")
  expect(terminal.captureCharFrame()).not.toContain("compile-only-sentinel")
  expect(terminal.captureCharFrame()).not.toContain("MT:Y.K9042C00KA0648G00")
  terminal.renderer.destroy()

  const pane = await testRender(
    () => (
      <SidePane
        pane="jobs"
        board={board}
        snapshot={{} as never}
        jobs={[job]}
        capture={{ active: false, entries: [] }}
        compact={false}
      />
    ),
    { width: 90, height: 18 }
  )
  teardown = () => pane.renderer.destroy()
  await pane.renderOnce()
  expect(pane.captureCharFrame()).toContain("compile-only-sentinel")
  expect(pane.captureCharFrame()).not.toContain("serial-only-sentinel")
})

test("an empty terminal buffer still reports an active serial connection", async () => {
  const board = makeBoardState("nrf")
  board.connection = "connected"
  board.port = "/dev/example"
  const view = await testRender(
    () => <SerialTerminal board={board} serialLines={[]} onReady={() => undefined} />,
    { width: 76, height: 12 }
  )
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  expect(view.captureCharFrame()).toContain("Serial console connected.")
  expect(view.captureCharFrame()).toContain("Type status to request fresh output.")
  expect(view.captureCharFrame()).not.toContain("No serial session.")
})

test("renders captured commissioning data in the dedicated pairing pane", async () => {
  const board = makeBoardState("nrf")
  board.pairing = { qrContent: "MT:Y.K9042C00KA0648G00", manualCode: "34970112332" }
  const view = await testRender(
    () => (
      <SidePane
        pane="pairing"
        board={board}
        snapshot={{} as never}
        jobs={[]}
        capture={{ active: false, entries: [] }}
        compact={false}
      />
    ),
    { width: 52, height: 26 }
  )
  teardown = () => view.renderer.destroy()
  await view.renderOnce()
  const frame = view.captureCharFrame()
  expect(frame).toContain("Scan with the commissioning app.")
  expect(frame).toContain("manual  34970112332")
  expect(frame).toContain("MT:Y.K9042C00KA0648G00")
  expect(frame).toMatch(/[█▀▄]/)
})
