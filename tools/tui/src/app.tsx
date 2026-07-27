import type { InputRenderable, KeyEvent, ScrollBoxRenderable, SelectRenderable, TabSelectRenderable } from "@opentui/core"
import { ErrorCorrectionLevel, registerQRCode } from "@opentui/qrcode/solid"
import { For, Show, createEffect, createMemo, createSignal, onCleanup, onMount } from "solid-js"
import { useRenderer, useTerminalDimensions } from "@opentui/solid"
import { existsSync } from "node:fs"
import { dirname, join } from "node:path"
import { adapters, commands, makeBoardState } from "./devices"
import { JobRunner } from "./jobs"
import { discoverSerialPortInfo, PosixSerialTransport, type SerialPortInfo } from "./serial"
import { inspectTarget, preferredAvailablePort, targetIds, targets, type TargetSnapshot } from "./targets"
import { SerialTerminalBuffer } from "./terminal"
import { severityColor, theme } from "./theme"
import type { BoardId, BoardState, Job, JobState, LogEntry, Severity } from "./types"
import { isDestructive, wizardBackAction, wizardView, type DestructiveAction, type WizardAction, type WizardStage } from "./wizard"

registerQRCode()

export type ActivityEntry = {
  timestamp: number
  kind: "command" | "message"
  severity: Severity
  text: string
}

type HelpRow = {
  command: string
  description: string
}

type PaneId = "off" | "overview" | "jobs" | "diagnostics" | "lab" | "capture" | "pairing"
type FocusArea = "wizard" | "command"
type CaptureSession = { active: boolean; target?: BoardId; startedAt?: number; entries: LogEntry[] }

const workspaceCommands: HelpRow[] = [
  { command: "wizard | wizard on | wizard off", description: "Return home, show the current guide, or hide it." },
  { command: "help | ?", description: "Show this complete command reference." },
  { command: "target nrf", description: "Select the nRF5340 DK Matter lock." },
  { command: "target esp32-lock", description: "Select the ESP32-S3 Matter lock." },
  { command: "target esp32-reader", description: "Select the standalone ESP32-S3 reader." },
  { command: "ports | scan", description: "Refresh compatible and incompatible serial devices." },
  { command: "connect", description: "Open a compatible serial connection." },
  { command: "disconnect", description: "Close the selected target's serial connection." },
  { command: "send <command>", description: "Send text directly to firmware, including its own help command." },
  { command: "terminal clear", description: "Clear only the selected board's emulated serial screen." },
  { command: "output clear | clear", description: "Clear TUI commands, notices, and the open help reference." },
  { command: "quit | exit", description: "Close the TUI and restore the terminal immediately." }
]

const benchCommands: HelpRow[] = [
  { command: "bootstrap", description: "Open the prerequisite confirmation; never downloads silently." },
  { command: "build | rebuild", description: "Incremental or pristine firmware build." },
  { command: "flash", description: "Flash the selected target, building first when needed." },
  { command: "flash-erase", description: "Full erase and flash." },
  { command: "rebuild-flash-erase", description: "Pristine rebuild, full erase, and flash." },
  { command: "factoryreset", description: "Ask the connected firmware to erase its credentials and reboot." },
  { command: "test", description: "Run the target's host-side test path." },
  { command: "diagnose", description: "Run the connected target's read-only diagnostic sweep." },
  { command: "lab on | lab off", description: "Control ESP transaction tracing and its live pane." },
  { command: "pair | codes", description: "Collect or reprint pairing data on Matter targets." },
  { command: "status | range", description: "Query the connected target." },
  { command: "cancel", description: "Cancel active and queued jobs." }
]

const workspaceViewCommands: HelpRow[] = [
  { command: "pane overview", description: "Prerequisites, artifact, and port inventory." },
  { command: "pane diagnostics", description: "Connection, firmware, range, trust, and counters." },
  { command: "pane jobs", description: "Active and recent build/flash/test output." },
  { command: "pane lab", description: "Structured Aliro Lab events." },
  { command: "pane capture", description: "Live in-memory serial capture." },
  { command: "pane pairing", description: "Scannable QR and manual commissioning code." },
  { command: "pane on | pane off", description: "Restore the last side pane or close it." },
  { command: "capture start|stop|clear", description: "Control the session-only serial capture." },
  { command: "PageUp / PageDown", description: "Scroll command output while fixed controls stay put." },
  { command: "Shift + PageUp / PageDown", description: "Scroll the live serial terminal." },
  { command: "Ctrl + PageUp / PageDown", description: "Scroll the jobs pane when it is open." },
  { command: "q", description: "Hide the wizard when its choices have focus." },
  { command: "Tab", description: "Move focus between wizard choices and command prompt." }
]

const clock = (value: number): string =>
  new Date(value).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" })

const findRepositoryRoot = (): string => {
  let candidate = process.cwd()
  while (true) {
    if (existsSync(join(candidate, "Makefile")) && existsSync(join(candidate, "modules"))) return candidate
    const parent = dirname(candidate)
    if (parent === candidate) return process.cwd()
    candidate = parent
  }
}

const delay = (milliseconds: number) => new Promise((resolve) => setTimeout(resolve, milliseconds))

function HelpGroup(props: { title: string; rows: HelpRow[] }) {
  return (
    <box style={{ flexDirection: "column", marginTop: 1 }}>
      <text style={{ fg: theme.muted }}>{props.title}</text>
      <For each={props.rows}>
        {(row) => (
          <box style={{ flexDirection: "row" }}>
            <text style={{ fg: theme.foreground, width: 29 }}>{row.command}</text>
            <text style={{ fg: theme.muted, flexGrow: 1 }}>{row.description}</text>
          </box>
        )}
      </For>
    </box>
  )
}

function HelpPanel() {
  return (
    <box
      style={{
        flexDirection: "column",
        border: true,
        borderStyle: "single",
        borderColor: theme.line,
        paddingLeft: 1,
        paddingRight: 1,
        marginTop: 1
      }}
    >
      <text style={{ fg: theme.foreground }}>command reference</text>
      <text style={{ fg: theme.muted }}>The wizard is the default. These commands keep every action directly accessible.</text>
      <HelpGroup title="guided workspace" rows={workspaceCommands} />
      <HelpGroup title="build and bench" rows={benchCommands} />
      <HelpGroup title="views and navigation" rows={workspaceViewCommands} />
      <For each={targetIds}>
        {(id) => (
          <HelpGroup
            title={targets[id].label}
            rows={commands[id].map(({ command, label }) => ({ command, description: label }))}
          />
        )}
      </For>
      <text style={{ fg: theme.warning, marginTop: 1 }}>
        Flashing, erasing, and factory reset always confirm first. `send &lt;command&gt;` is the one deliberate bypass.
      </text>
      <text style={{ fg: theme.muted }}>Other text is sent unchanged to the selected connected target.</text>
    </box>
  )
}

export function CommandOutput(props: {
  activity: ActivityEntry[]
  showHelp: boolean
  onReady: (output: ScrollBoxRenderable) => void
}) {
  const colorFor = (entry: ActivityEntry) =>
    entry.kind === "message" && entry.severity === "info" ? theme.muted : severityColor[entry.severity]

  const prefixFor = (entry: ActivityEntry) => {
    if (entry.kind === "command") return "> "
    if (entry.kind === "message" && entry.severity === "error") return "error: "
    if (entry.kind === "message" && entry.severity === "warning") return "notice: "
    return ""
  }

  return (
    <box
      style={{
        flexDirection: "column",
        flexGrow: 1
      }}
    >
      <box
        style={{
          flexDirection: "row",
          justifyContent: "space-between",
          height: 1,
          flexShrink: 0,
          marginBottom: 1
        }}
      >
        <text style={{ fg: theme.foreground }}>command output</text>
        <text style={{ fg: theme.muted }}>PageUp/PageDown scroll</text>
      </box>
      <scrollbox
        ref={(value) => {
          value.verticalScrollBar.visible = true
          props.onReady(value)
        }}
        style={{
          flexGrow: 1,
          border: true,
          borderStyle: "single",
          borderColor: theme.line,
          paddingLeft: 1,
          paddingRight: 1
        }}
        stickyScroll={!props.showHelp}
        stickyStart={props.showHelp || props.activity.length === 0 ? "top" : "bottom"}
        viewportCulling
      >
        <Show when={props.activity.length === 0 && !props.showHelp}>
          <box style={{ flexDirection: "column" }}>
            <text style={{ fg: theme.foreground }}>No TUI activity yet.</text>
            <text style={{ fg: theme.muted }}>Commands, explanations, errors, and help render here.</text>
            <text style={{ fg: theme.muted }}>Firmware traffic stays in the separate serial terminal.</text>
          </box>
        </Show>
        <For each={props.activity.slice(-300)}>
          {(entry) => (
            <text style={{ fg: colorFor(entry) }}>
              <span style={{ fg: theme.muted }}>{clock(entry.timestamp)} </span>
              {prefixFor(entry)}
              {entry.text}
            </text>
          )}
        </For>
        <Show when={props.showHelp}>
          <HelpPanel />
        </Show>
      </scrollbox>
    </box>
  )
}

export function SerialTerminal(props: {
  board: BoardState
  serialLines: string[]
  onReady: (output: ScrollBoxRenderable) => void
}) {
  return (
    <box style={{ flexDirection: "column", flexGrow: 1 }}>
      <box
        style={{
          flexDirection: "row",
          justifyContent: "space-between",
          height: 1,
          flexShrink: 0,
          marginBottom: 1
        }}
      >
        <text style={{ fg: theme.foreground }}>serial terminal</text>
        <text style={{ fg: theme.muted }}>
          {props.board.connection === "connected"
            ? `live · ${props.board.port ?? "serial"} · 115200 baud · VT`
            : `${props.board.connection} · Shift+PageUp/PageDown scroll`}
        </text>
      </box>
      <scrollbox
        ref={(value) => {
          value.verticalScrollBar.visible = true
          props.onReady(value)
        }}
        style={{
          flexGrow: 1,
          border: true,
          borderStyle: "single",
          borderColor: props.board.connection === "connected" ? theme.foreground : theme.line,
          paddingLeft: 1,
          paddingRight: 1
        }}
        stickyScroll
        stickyStart={props.serialLines.length === 0 ? "top" : "bottom"}
        viewportCulling
      >
        <Show when={props.serialLines.length === 0}>
          <Show
            when={props.board.connection === "connected"}
            fallback={
              <box style={{ flexDirection: "column" }}>
                <text style={{ fg: theme.foreground }}>
                  {props.board.connection === "connecting" ? "Opening serial console." : "No serial session."}
                </text>
                <text style={{ fg: theme.muted }}>Type connect below or choose Connect in the wizard.</text>
                <text style={{ fg: theme.muted }}>Firmware boot logs, prompts, and command replies render only here.</text>
              </box>
            }
          >
            <box style={{ flexDirection: "column" }}>
              <text style={{ fg: theme.foreground }}>Serial console connected.</text>
              <text style={{ fg: theme.muted }}>The emulated screen is empty. Type status to request fresh output.</text>
              <text style={{ fg: theme.muted }}>New firmware logs and command replies will render here.</text>
            </box>
          </Show>
        </Show>
        <box style={{ flexDirection: "column" }}>
          <For each={props.serialLines}>{(line) => <text style={{ fg: theme.foreground }}>{line || " "}</text>}</For>
        </box>
      </scrollbox>
    </box>
  )
}

export function PairingPayload(props: { pairing?: BoardState["pairing"]; compact?: boolean }) {
  return (
    <Show when={props.pairing?.qrContent}>
      {(content) => (
        <box
          style={{
            flexDirection: "column"
          }}
        >
          <text style={{ fg: theme.muted }}>Scan with the commissioning app.</text>
          <qr_code
            content={content()}
            errorCorrectionLevel={ErrorCorrectionLevel.M}
            quietZone={4}
            fit="contain"
            foregroundColor={theme.foreground}
            backgroundColor={theme.background}
            width="100%"
            height={props.compact ? 5 : 18}
          />
          <Show when={props.pairing?.manualCode}>
            <text style={{ fg: theme.foreground }}>{`manual  ${props.pairing?.manualCode}`}</text>
          </Show>
          <text style={{ fg: theme.muted }}>{content()}</text>
        </box>
      )}
    </Show>
  )
}

export function WizardCard(props: {
  view: ReturnType<typeof wizardView>
  focused: boolean
  short: boolean
  pulse: string
  canGoBack: boolean
  onReady: (select: SelectRenderable) => void
  onAction: (action: WizardAction) => void
}) {
  return (
    <box
      style={{
        height: props.short ? 7 : 11,
        flexShrink: 0,
        flexDirection: "column",
        border: true,
        borderStyle: "single",
        borderColor: props.view.danger ? theme.danger : props.focused ? theme.foreground : theme.line,
        paddingLeft: 1,
        paddingRight: 1,
        marginTop: 1
      }}
    >
      <box style={{ flexDirection: "row", justifyContent: "space-between", height: 1, flexShrink: 0 }}>
        <text style={{ fg: props.view.danger ? theme.danger : theme.muted }}>{props.view.eyebrow}</text>
        <text style={{ fg: theme.muted }}>
          {`${props.pulse} q close · ${props.canGoBack ? "← back · " : ""}↑↓ choose · Enter · Tab command`}
        </text>
      </box>
      <text style={{ fg: props.view.danger ? theme.danger : theme.foreground, height: 1, flexShrink: 0 }}>
        {props.view.title}
      </text>
      <Show when={!props.short}>
        <text style={{ fg: theme.muted, height: 2, flexShrink: 0 }}>{props.view.detail}</text>
      </Show>
      <select
        ref={props.onReady}
        focused={props.focused}
        options={props.view.choices}
        height={props.short ? 3 : 5}
        width="100%"
        backgroundColor="transparent"
        focusedBackgroundColor="transparent"
        selectedBackgroundColor="transparent"
        textColor={theme.foreground}
        focusedTextColor={theme.foreground}
        selectedTextColor={theme.foreground}
        descriptionColor={theme.muted}
        selectedDescriptionColor={theme.muted}
        showDescription={!props.short}
        showScrollIndicator
        wrapSelection
        onSelect={(_index, option) => {
          if (option?.value) props.onAction(option.value as WizardAction)
        }}
      />
    </box>
  )
}

export function SidePane(props: {
  pane: PaneId
  board: BoardState
  snapshot: TargetSnapshot
  jobs: Job[]
  capture: CaptureSession
  compact: boolean
  onJobsReady?: (output: ScrollBoxRenderable) => void
}) {
  const title = () => (props.pane === "off" ? "" : props.pane)
  return (
    <box
      style={{
        flexDirection: "column",
        border: true,
        borderStyle: "single",
        borderColor: theme.line,
        paddingLeft: 1,
        paddingRight: 1,
        width: props.compact ? "100%" : props.pane === "jobs" ? "50%" : 42,
        height: props.compact ? 9 : "100%",
        flexShrink: 0,
        marginLeft: props.compact ? 0 : 1,
        marginTop: props.compact ? 1 : 0
      }}
    >
      <box style={{ flexDirection: "row", justifyContent: "space-between" }}>
        <text style={{ fg: theme.foreground }}>{title()}</text>
        <text style={{ fg: theme.muted }}>pane off</text>
      </box>

      <Show when={props.pane === "overview"}>
        <text style={{ fg: theme.muted }}>{props.snapshot.setupDetail}</text>
        <text style={{ fg: theme.foreground }}>{`artifact  ${props.snapshot.artifact}`}</text>
        <text style={{ fg: theme.muted }}>{props.snapshot.artifactPath}</text>
        <text style={{ fg: theme.foreground }}>{`ports     ${props.snapshot.compatiblePorts.length} compatible`}</text>
        <For each={props.snapshot.compatiblePorts}>{(port) => <text style={{ fg: theme.muted }}>{port}</text>}</For>
        <Show when={props.snapshot.incompatiblePorts.length > 0}>
          <text style={{ fg: theme.warning }}>{`${props.snapshot.incompatiblePorts.length} incompatible port(s) ignored`}</text>
        </Show>
        <text style={{ fg: theme.muted, marginTop: 1 }}>{targets[props.board.id].setupGuide}</text>
      </Show>

      <Show when={props.pane === "diagnostics"}>
        <text style={{ fg: theme.foreground }}>{props.board.label}</text>
        <text style={{ fg: theme.muted }}>{`connection  ${props.board.connection}`}</text>
        <text style={{ fg: theme.muted }}>{`port        ${props.board.port ?? "none"}`}</text>
        <text style={{ fg: theme.muted }}>{`firmware    ${props.board.firmware ?? "unknown"}`}</text>
        <text style={{ fg: theme.muted }}>{`range       ${props.board.lastRangeCm === undefined ? "no sample" : `${props.board.lastRangeCm} cm`}`}</text>
        <text style={{ fg: theme.muted }}>{`trusted     ${props.board.trustedRange === undefined ? "unknown" : props.board.trustedRange ? "yes" : "no"}`}</text>
        <Show when={props.board.counters}>
          {(counters) => (
            <text style={{ fg: theme.muted }}>{`rx          ${counters().good} ok · ${counters().error} err · ${counters().timeout} timeout`}</text>
          )}
        </Show>
        <text style={{ fg: theme.foreground, marginTop: 1 }}>{props.board.statusLine}</text>
      </Show>

      <Show when={props.pane === "jobs"}>
        <scrollbox
          ref={(value) => {
            value.verticalScrollBar.visible = true
            props.onJobsReady?.(value)
          }}
          style={{ flexGrow: 1 }}
          stickyScroll
          stickyStart="bottom"
          viewportCulling
        >
          <Show when={props.jobs.length === 0}>
            <text style={{ fg: theme.muted }}>No jobs in this session.</text>
          </Show>
          <For each={props.jobs.slice().reverse()}>
            {(job) => (
              <box style={{ flexDirection: "column", marginBottom: 1 }}>
                <text style={{ fg: job.state === "failed" ? theme.danger : job.state === "passed" ? theme.success : theme.foreground }}>
                  {`${job.state} · ${job.label}`}
                </text>
                <text style={{ fg: theme.muted }}>{`$ ${job.command.join(" ")}`}</text>
                <For each={job.output}>{(line) => <text style={{ fg: theme.foreground }}>{line || " "}</text>}</For>
              </box>
            )}
          </For>
        </scrollbox>
      </Show>

      <Show when={props.pane === "lab"}>
        <text style={{ fg: theme.muted }}>Send lab on before a walk-up and lab off afterward.</text>
        <Show when={props.board.alab.length === 0}>
          <text style={{ fg: theme.muted, marginTop: 1 }}>No [ALAB] events yet.</text>
        </Show>
        <For each={props.board.alab.slice(-(props.compact ? 3 : 16)).reverse()}>
          {(event) => (
            <text style={{ fg: theme.foreground }}>{`${event.timestampUs}  ${event.name}  ${Object.entries(event.attributes)
              .map(([key, value]) => `${key}=${value}`)
              .join(" ")}`}</text>
          )}
        </For>
      </Show>

      <Show when={props.pane === "capture"}>
        <text style={{ fg: props.capture.active ? theme.success : theme.muted }}>
          {props.capture.active ? `recording ${props.capture.entries.length} lines` : `stopped · ${props.capture.entries.length} lines retained`}
        </text>
        <text style={{ fg: theme.muted }}>capture start · capture stop · capture clear</text>
        <For each={props.capture.entries.slice(-(props.compact ? 3 : 18))}>
          {(entry) => <text style={{ fg: severityColor[entry.severity] }}>{entry.text}</text>}
        </For>
      </Show>

      <Show when={props.pane === "pairing"}>
        <Show
          when={props.board.pairing?.qrContent}
          fallback={<text style={{ fg: theme.muted }}>No onboarding payload captured. Use pair and connect to the Matter target.</text>}
        >
          <PairingPayload pairing={props.board.pairing} compact={props.compact} />
        </Show>
      </Show>
    </box>
  )
}

export function App(
  props: {
    autoConnect?: boolean
    discoverPorts?: () => Promise<SerialPortInfo[]>
  } = {}
) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const root = findRepositoryRoot()
  const [activeBoard, setActiveBoard] = createSignal<BoardId>("nrf")
  const [boards, setBoards] = createSignal<Record<BoardId, BoardState>>({
    nrf: makeBoardState("nrf"),
    "esp32-lock": makeBoardState("esp32-lock"),
    "esp32-reader": makeBoardState("esp32-reader")
  })
  const [jobs, setJobs] = createSignal<Job[]>([])
  const [prompt, setPrompt] = createSignal("")
  const [activity, setActivity] = createSignal<ActivityEntry[]>([])
  const [showHelp, setShowHelp] = createSignal(false)
  const [ports, setPorts] = createSignal<SerialPortInfo[]>([])
  const [snapshot, setSnapshot] = createSignal(inspectTarget(root, "nrf", []))
  const [wizardStage, setWizardStage] = createSignal<WizardStage>("choose-target")
  const [wizardVisible, setWizardVisible] = createSignal(true)
  const [inventoryPending, setInventoryPending] = createSignal(false)
  const [recovery, setRecovery] = createSignal<string>()
  // The destructive action awaiting confirmation. Nothing reads it except the
  // confirm stage, so it cannot leak into a later run: askConfirmation() is the
  // only writer, and the accept path clears it before doing any work.
  const [pending, setPending] = createSignal<DestructiveAction>()
  const [focusArea, setFocusArea] = createSignal<FocusArea>("wizard")
  const [activePane, setActivePane] = createSignal<PaneId>("off")
  const [lastPane, setLastPane] = createSignal<Exclude<PaneId, "off">>("diagnostics")
  const [capture, setCapture] = createSignal<CaptureSession>({ active: false, entries: [] })
  const [pulseIndex, setPulseIndex] = createSignal(0)
  const [workflowClaimed, setWorkflowClaimed] = createSignal(false)
  const [workflowCancelled, setWorkflowCancelled] = createSignal(false)
  const transports = new Map<BoardId, PosixSerialTransport>()
  const connectionAttempts = new Map<BoardId, Promise<boolean>>()
  const openingPorts = new Set<string>()
  const terminals = new Map<BoardId, SerialTerminalBuffer>(
    targetIds.map((id) => [id, new SerialTerminalBuffer()])
  )
  const [serialLines, setSerialLines] = createSignal<Record<BoardId, string[]>>({
    nrf: [],
    "esp32-lock": [],
    "esp32-reader": []
  })
  const runner = new JobRunner()
  let disposed = false
  let input: InputRenderable | undefined
  let boardTabs: TabSelectRenderable | undefined
  let wizardSelect: SelectRenderable | undefined
  let commandOutput: ScrollBoxRenderable | undefined
  let serialOutput: ScrollBoxRenderable | undefined
  let jobsOutput: ScrollBoxRenderable | undefined
  let selectionRevision = 0

  const selected = createMemo(() => boards()[activeBoard()])
  const compact = createMemo(() => dimensions().width < 108)
  const short = createMemo(() => dimensions().height < 32)
  const primaryStacked = createMemo(() => compact() || activePane() !== "off")
  const runningJob = createMemo(() => jobs().find((job) => job.state === "running" || job.state === "queued"))
  const activeWorkflowState = createMemo<JobState | undefined>(() => runningJob()?.state ?? (workflowClaimed() ? "queued" : undefined))
  const workflowBusy = () => activeWorkflowState() !== undefined
  const wizardContext = createMemo(() => ({
    target: activeBoard(),
    snapshot: snapshot(),
    connection: selected().connection,
    jobState: activeWorkflowState(),
    inventoryPending: inventoryPending(),
    pairingReady: Boolean(selected().pairing?.qrContent),
    recovery: recovery(),
    pending: pending()
  }))
  const currentWizardView = createMemo(() => wizardView(wizardStage(), wizardContext()))
  const currentBackAction = createMemo(() => wizardBackAction(wizardStage(), wizardContext()))
  const boardOptions = createMemo(() =>
    targetIds.map((id) => ({
      name: targets[id].shortLabel,
      value: id,
      description: boards()[id].connection
    }))
  )

  const patchBoard = (id: BoardId, fn: (value: BoardState) => BoardState) =>
    setBoards((current) => ({ ...current, [id]: fn(current[id]) }))

  const report = (text: string, severity: Severity = "info") =>
    setActivity((entries) => [...entries.slice(-299), { timestamp: Date.now(), kind: "message", severity, text }])

  const recordCommand = (text: string) =>
    setActivity((entries) => [...entries.slice(-299), { timestamp: Date.now(), kind: "command", severity: "info", text }])

  const clearSerialTerminal = (id = activeBoard()) => {
    const lines = terminals.get(id)?.clear() ?? []
    setSerialLines((current) => ({ ...current, [id]: lines }))
  }

  const clearCommandOutput = () => {
    setActivity([])
    setShowHelp(false)
  }

  const focusWizard = () => {
    if (!wizardVisible()) {
      focusCommand()
      return
    }
    setFocusArea("wizard")
    input?.blur()
    wizardSelect?.focus()
  }

  const focusCommand = () => {
    setFocusArea("command")
    wizardSelect?.blur()
    input?.focus()
  }

  const hideWizard = () => {
    setWizardVisible(false)
    wizardSelect = undefined
    focusCommand()
  }

  const rejectDuringWorkflow = (requested: string): boolean => {
    if (!workflowBusy()) return false
    const active = runningJob()
    report(
      `${active?.label ?? "The current workflow"} is ${active?.state ?? "starting"}. ${requested} was not started. Wait for it to finish or choose Cancel active jobs.`,
      "warning"
    )
    setWizardStage("running")
    setActivePane("jobs")
    focusWizard()
    return true
  }

  const cancelWorkflow = () => {
    if (!workflowBusy()) {
      report("No build, test, or flash workflow is active.", "warning")
      return
    }
    setWorkflowCancelled(true)
    runner.cancelAll()
    report("The active workflow is being cancelled. No later workflow step will start.", "warning")
    setWizardStage("running")
  }

  const refreshInventory = async (announce = false, target = activeBoard()): Promise<TargetSnapshot> => {
    const inventory = await (props.discoverPorts?.() ?? discoverSerialPortInfo())
    setPorts(inventory)
    const next = inspectTarget(root, target, inventory)
    if (target === activeBoard()) setSnapshot(next)
    if (announce) {
      report(
        next.compatiblePorts.length === 0
          ? `No compatible serial device found for ${targets[target].label}. ${next.incompatiblePorts.length} incompatible device(s) were ignored.`
          : `${next.compatiblePorts.length} compatible serial device${next.compatiblePorts.length === 1 ? "" : "s"} found for ${targets[target].label}.`,
        next.compatiblePorts.length === 0 ? "warning" : "success"
      )
    }
    return next
  }

  const closeTransport = async (id: BoardId) => {
    const transport = transports.get(id)
    if (!transport) return
    transports.delete(id)
    await transport.close()
    patchBoard(id, (board) => ({ ...board, connection: "disconnected", port: undefined, statusLine: "Disconnected" }))
  }

  const closeAllTransports = () => {
    const active = [...transports.values()]
    transports.clear()
    for (const transport of active) void transport.close()
  }

  const quit = () => {
    runner.cancelAll()
    closeAllTransports()
    renderer.destroy()
  }

  const selectBoard = (id: BoardId, updateTabs = true) => {
    if (id !== activeBoard() && rejectDuringWorkflow(`Switching to ${targets[id].label}`)) {
      queueMicrotask(() => boardTabs?.setSelectedIndex(targetIds.indexOf(activeBoard())))
      return
    }
    const revision = ++selectionRevision
    setInventoryPending(true)
    setActiveBoard(id)
    setSnapshot(inspectTarget(root, id, ports()))
    setRecovery(undefined)
    setWizardStage("home")
    if (updateTabs) boardTabs?.setSelectedIndex(targetIds.indexOf(id))
    focusWizard()
    void refreshInventory(false, id)
      .then((next) => {
        if (disposed || revision !== selectionRevision || activeBoard() !== id) return
        if (props.autoConnect === false) return
        if (transports.has(id) || connectionAttempts.has(id)) return
        const used = [...transports.values()].map((transport) => transport.path)
        const port = preferredAvailablePort(next.compatiblePorts, [...used, ...openingPorts])
        if (!port) {
          if (next.compatiblePorts.length > 0) {
            report(`${targets[id].label} has no unused compatible serial console. Close the other session or choose another port.`, "warning")
          }
          return
        }
        report(`Opening the preferred ${targets[id].label} serial console.`)
        void connect(id, port, false, next)
      })
      .catch((error) => {
        if (revision !== selectionRevision) return
        const message = error instanceof Error ? error.message : "Inventory scan failed"
        report(`Could not inspect ${targets[id].label}: ${message}`, "error")
      })
      .finally(() => {
        if (revision === selectionRevision) setInventoryPending(false)
      })
  }

  const connectOnce = async (
    id = activeBoard(),
    requestedPort?: string,
    quiet = false,
    knownSnapshot?: TargetSnapshot
  ): Promise<boolean> => {
    const existing = transports.get(id)
    if (existing) {
      try {
        await existing.write(adapters[id].commands[0].command)
        if (!quiet) report(`${boards()[id].label} is already connected. A fresh status request was sent.`, "success")
        return true
      } catch {
        await closeTransport(id)
        if (!quiet) report(`${boards()[id].label} had a stale serial session. Reopening it now.`, "warning")
      }
    }
    patchBoard(id, (board) => ({ ...board, connection: "connecting", statusLine: "Looking for a compatible serial port" }))
    const next = knownSnapshot ?? (await refreshInventory(false, id))
    const used = new Set([...transports.values()].map((transport) => transport.path).concat([...openingPorts]))
    const port = requestedPort ?? next.compatiblePorts.find((candidate) => !used.has(candidate))
    if (!port || !next.compatiblePorts.includes(port) || used.has(port)) {
      patchBoard(id, (board) => ({ ...board, connection: "error", statusLine: "No compatible serial device found" }))
      if (!quiet) report(`No compatible serial device found for ${boards()[id].label}. Use ports to scan again.`, "error")
      return false
    }

    openingPorts.add(port)
    const transport = new PosixSerialTransport(port)
    transport.on((event) => {
      if (event.type === "data") {
        const terminal = terminals.get(id)
        if (terminal) {
          void terminal.write(event.data).then((lines) => {
            if (!disposed) setSerialLines((current) => ({ ...current, [id]: lines }))
          })
        }
      }
      if (event.type === "line") {
        const previousPairing = boards()[id].pairing?.qrContent
        const updated = adapters[id].ingest(boards()[id], event.line)
        patchBoard(id, () => updated)
        if (capture().active && capture().target === id) {
          const latest = updated.logs.at(-1)
          if (latest) setCapture((current) => ({ ...current, entries: [...current.entries.slice(-999), latest] }))
        }
        if (!previousPairing && updated.pairing?.qrContent) {
          report(`Pairing payload captured from ${updated.label}.`, "success")
          setActivePane("pairing")
        }
      }
      if (event.type === "error") {
        patchBoard(id, (board) => ({ ...board, connection: "error", statusLine: event.error.message }))
        report(`${boards()[id].label} serial error: ${event.error.message}`, "error")
      }
      if (event.type === "close" && transports.get(id) === transport) {
        transports.delete(id)
        patchBoard(id, (board) => ({ ...board, connection: "disconnected", port: undefined, statusLine: "Serial connection closed" }))
      }
    })
    try {
      await transport.open()
      transports.set(id, transport)
      patchBoard(id, (board) => ({ ...board, connection: "connected", port, statusLine: "Connected" }))
      await transport.write(adapters[id].commands[0].command)
      if (!quiet) report(`${boards()[id].label} connected on ${port}.`, "success")
      return true
    } catch (error) {
      const message = error instanceof Error ? error.message : "Serial connection failed"
      patchBoard(id, (board) => ({ ...board, connection: "error", statusLine: message }))
      await transport.close()
      if (!quiet) report(`Could not connect ${boards()[id].label}: ${message}`, "error")
      return false
    } finally {
      openingPorts.delete(port)
    }
  }

  const connect = (
    id = activeBoard(),
    requestedPort?: string,
    quiet = false,
    knownSnapshot?: TargetSnapshot
  ): Promise<boolean> => {
    const pending = connectionAttempts.get(id)
    if (pending) return pending
    const attempt = connectOnce(id, requestedPort, quiet, knownSnapshot)
    connectionAttempts.set(id, attempt)
    const clear = () => {
      if (connectionAttempts.get(id) === attempt) connectionAttempts.delete(id)
    }
    void attempt.then(clear, clear)
    return attempt
  }

  const reconnectAfterFlash = async (id: BoardId) => {
    for (let attempt = 0; attempt < 20; attempt++) {
      if (attempt > 0) await delay(200)
      if (await connect(id, undefined, true)) {
        report(`${boards()[id].label} reconnected after flashing.`, "success")
        if (targets[id].supportsPairing) void pairingCodes(id, true)
        return
      }
    }
    report(`The flash completed, but the serial port has not returned yet. Use connect when the board finishes rebooting.`, "warning")
  }

  const disconnect = async () => {
    const id = activeBoard()
    if (!transports.has(id)) {
      report(`${boards()[id].label} is not connected.`, "warning")
      return
    }
    await closeTransport(id)
    report(`${boards()[id].label} disconnected.`)
  }

  const send = async (command: string, id = activeBoard()) => {
    const transport = transports.get(id)
    if (!transport) {
      report(`${boards()[id].label} is not connected. The wizard can scan and connect it.`, "error")
      setWizardStage("home")
      focusWizard()
      return
    }
    try {
      await transport.write(command)
    } catch (error) {
      const message = error instanceof Error ? error.message : "Serial write failed"
      patchBoard(id, (board) => ({ ...board, connection: "error", statusLine: message }))
      report(`Could not send command: ${message}`, "error")
    }
  }

  type WorkflowJob = "bootstrap" | "build" | "rebuild" | "test" | "flash" | "flash-erase"

  const commandFor = (kind: WorkflowJob, id: BoardId): string[] => {
    const commandsForTarget = targets[id].commands
    const base =
      kind === "bootstrap"
        ? commandsForTarget.bootstrap
        : kind === "flash-erase"
          ? commandsForTarget.flashErase
          : commandsForTarget[kind]
    if (!base) return []
    const command = [...base]
    if (id !== "nrf") {
      const configuredIdfExport = process.env.IDF_EXPORT ?? (process.env.IDF_PATH ? join(process.env.IDF_PATH, "export.sh") : undefined)
      if (configuredIdfExport) command.push(`IDF_EXPORT=${configuredIdfExport}`)
      if (id === "esp32-lock" && process.env.ESP_MATTER_PATH) command.push(`ESP_MATTER_PATH=${process.env.ESP_MATTER_PATH}`)
      if (kind === "flash" || kind === "flash-erase") {
        const port = boards()[id].port ?? snapshot().compatiblePorts[0]
        if (port) command.push(`PORT=${port}`)
      }
    }
    return command
  }

  const runSingleJob = async (kind: WorkflowJob, id: BoardId): Promise<Job> => {
    if (kind === "flash" || kind === "flash-erase") {
      if (transports.has(id)) {
        await closeTransport(id)
        report(`Released ${boards()[id].label} serial before programming.`)
      }
    }
    const command = commandFor(kind, id)
    report(`${kind} started for ${boards()[id].label}.`)
    setWizardStage("running")
    setActivePane("jobs")
    focusWizard()
    return runner.run(`${boards()[id].label} ${kind}`, command, root)
  }

  const executeWorkflow = async (kind: WorkflowJob, pristineBeforeErase = false) => {
    const id = activeBoard()
    let current = await refreshInventory(false, id)
    const stopBeforeNextJob = (): boolean => {
      if (!workflowCancelled()) return false
      report("Workflow cancelled before the next command started.", "warning")
      setRecovery("The workflow was cancelled. No later build or programming step was started.")
      setWizardStage("recovery")
      return true
    }
    if (stopBeforeNextJob()) return
    if (kind !== "bootstrap" && kind !== "test" && !current.setupReady) {
      if (targets[id].commands.bootstrap) {
        report("Firmware prerequisites are missing. The wizard moved to the safe bootstrap confirmation.", "warning")
        setWizardStage("bootstrap-confirm")
      } else {
        report(`${current.setupDetail}. Open the overview pane for the repository setup guide.`, "error")
        setRecovery("This repository has no automatic ESP-IDF installer. Configure the official toolchains, then inspect again.")
        setWizardStage("recovery")
        setActivePane("overview")
      }
      focusWizard()
      return
    }
    if (pristineBeforeErase) {
      if (stopBeforeNextJob()) return
      report("Creating a pristine artifact from the current repository state before the full erase.", "warning")
      const rebuild = await runSingleJob("rebuild", id)
      current = await refreshInventory(false, id)
      if (rebuild.state !== "passed") {
        report("The pristine rebuild failed, so the board was not erased or flashed.", "error")
        setRecovery("The pristine rebuild failed. No destructive programming step was started.")
        setWizardStage("recovery")
        return
      }
      report(`Pristine rebuild completed for ${boards()[id].label}.`, "success")
      if (stopBeforeNextJob()) return
    }
    if (!pristineBeforeErase && (kind === "flash" || kind === "flash-erase") && current.artifact === "missing") {
      if (stopBeforeNextJob()) return
      report("No build artifact exists. Building the current repository state before flashing.", "warning")
      const build = await runSingleJob("build", id)
      current = await refreshInventory(false, id)
      if (build.state !== "passed") {
        setRecovery("The prerequisite build failed, so the board was not flashed.")
        setWizardStage("recovery")
        return
      }
      if (stopBeforeNextJob()) return
    }

    if (stopBeforeNextJob()) return
    const job = await runSingleJob(kind, id)
    current = await refreshInventory(false, id)
    if (job.state === "cancelled") {
      report(`${kind} cancelled.`, "warning")
      setRecovery("The task was cancelled. No later wizard step was started.")
      setWizardStage("recovery")
      return
    }
    if (job.state !== "passed") {
      report(`${kind} failed. The full command output is retained in the jobs pane.`, "error")
      setRecovery(`${kind} did not complete. The wizard has returned to paths that do not depend on it.`)
      setWizardStage("recovery")
      return
    }

    report(`${kind} completed for ${boards()[id].label}.`, "success")
    setRecovery(undefined)
    if (kind === "build" || kind === "rebuild") {
      setWizardStage(current.compatiblePorts.length > 0 ? "flash-choice" : "home")
    } else if (kind === "flash" || kind === "flash-erase") {
      if (kind === "flash-erase") patchBoard(id, (board) => ({ ...board, pairing: undefined }))
      setWizardStage(targets[id].supportsPairing ? "pair" : "home")
      void reconnectAfterFlash(id)
    } else {
      setWizardStage("home")
    }
  }

  const runWorkflow = async (kind: WorkflowJob, pristineBeforeErase = false) => {
    if (rejectDuringWorkflow(`A ${kind} workflow`)) return
    setWorkflowCancelled(false)
    setWorkflowClaimed(true)
    setWizardStage("running")
    setActivePane("jobs")
    focusWizard()
    try {
      await executeWorkflow(kind, pristineBeforeErase)
    } finally {
      setWorkflowClaimed(false)
      setWorkflowCancelled(false)
    }
  }

  // Single entry point for every destructive action, from the wizard and from
  // the command prompt alike. Nothing calls the run* functions below directly,
  // so a new destructive path cannot accidentally ship without a confirmation.
  const askConfirmation = (action: DestructiveAction): void => {
    if (rejectDuringWorkflow(`A ${action} request`)) return
    if (action === "factory-reset" && !targets[activeBoard()].supportsFactoryReset) {
      report(`${targets[activeBoard()].label} has no factory-reset command.`, "warning")
      return
    }
    setPending(action)
    setWizardStage("confirm")
    setWizardVisible(true)
    focusWizard()
  }

  const runFactoryReset = async () => {
    const id = activeBoard()
    const command = commands[id].find(({ id: commandId }) => commandId === "factory-reset")?.command
    if (!command) {
      report(`${boards()[id].label} has no factory-reset command.`, "error")
      return
    }
    if (boards()[id].connection !== "connected" && !(await connect(id))) {
      report(`Could not factory reset ${boards()[id].label} because it is not connected.`, "error")
      setWizardStage("recovery")
      setRecovery("A factory reset needs a live serial connection. Check the port inventory, then try again.")
      return
    }
    report(`Factory reset sent to ${boards()[id].label}. It erases its credentials and reboots.`, "warning")
    await send(command, id)
    // The board comes back unprovisioned, so the captured onboarding code is
    // stale from this moment: keep it out of the pairing pane rather than let
    // someone scan a QR the firmware no longer honours.
    patchBoard(id, (board) => ({ ...board, pairing: undefined }))
    setWizardStage(targets[id].supportsPairing ? "pair" : "home")
  }

  const runDestructive = (action: DestructiveAction): void => {
    setPending(undefined)
    if (action === "factory-reset") return void runFactoryReset()
    if (action === "rebuild-flash-erase") return void runWorkflow("flash-erase", true)
    void runWorkflow(action)
  }

  const pairingCodes = async (id = activeBoard(), force = false) => {
    if (!targets[id].supportsPairing) {
      report(`${targets[id].label} has no Matter onboarding flow. Choose a Matter lock target to pair.`, "warning")
      return
    }
    if (id === activeBoard()) {
      setWizardStage("pair")
      setActivePane("pairing")
      focusWizard()
    }
    if (!force && boards()[id].pairing?.qrContent) return
    if (boards()[id].connection !== "connected" && !(await connect(id))) {
      report(`Could not request onboarding codes because ${boards()[id].label} is not connected.`, "error")
      return
    }

    const command = commands[id].find(({ id: commandId }) => commandId === "codes")?.command
    if (!command) {
      report(`${boards()[id].label} does not expose an onboarding-code command.`, "error")
      return
    }
    report(`Requesting onboarding codes from ${boards()[id].label}.`)
    for (let attempt = 0; attempt < 3; attempt++) {
      await delay(attempt === 0 ? 500 : 800)
      await send(command, id)
      const deadline = Date.now() + 1600
      while (Date.now() < deadline) {
        if (boards()[id].pairing?.qrContent) {
          report(`Onboarding QR and pairing data received from ${boards()[id].label}.`, "success")
          if (id === activeBoard()) setActivePane("pairing")
          return
        }
        await delay(100)
      }
    }
    report(
      `${boards()[id].label} did not return onboarding data. The serial connection is still listening; retry codes or press RESET once.`,
      "error"
    )
  }

  const runDiagnostics = async () => {
    const id = activeBoard()
    if (selected().connection !== "connected" && !(await connect(id))) {
      setRecovery("Diagnostics need a compatible serial connection. Check the port inventory or continue with host tests.")
      setWizardStage("recovery")
      return
    }
    const diagnosticCommands: Record<BoardId, string[]> = {
      nrf: ["aliro status", "aliro rx", "aliro chip", "aliro range"],
      "esp32-lock": ["status", "range", "aliro prov"],
      "esp32-reader": ["status", "range", "aliro-prov"]
    }
    setActivePane("diagnostics")
    setWizardStage("diagnostics")
    report(`Read-only diagnostic sweep started for ${boards()[id].label}.`)
    for (const command of diagnosticCommands[id]) {
      await send(command)
      await delay(120)
    }
    report("Diagnostic requests sent. Live results remain in the output and diagnostics pane.", "success")
  }

  const controlLab = async (enabled: boolean) => {
    const id = activeBoard()
    if (id === "nrf") {
      report("This nRF image exposes its diagnostics through aliro commands rather than the ESP Aliro Lab console.", "warning")
      return
    }
    if (selected().connection !== "connected" && !(await connect(id))) {
      setRecovery("Aliro Lab needs a compatible serial connection.")
      setWizardStage("recovery")
      return
    }
    await send(`lab ${enabled ? "on" : "off"}`)
    setActivePane("lab")
    setWizardStage("diagnostics")
    report(
      enabled
        ? "Aliro Lab enabled. Walk up, exercise the lock, then choose Stop Aliro Lab to remove trace overhead."
        : "Aliro Lab disabled.",
      enabled ? "warning" : "success"
    )
  }

  const controlCapture = (action: "start" | "stop" | "clear") => {
    if (action === "start") {
      setCapture({ active: true, target: activeBoard(), startedAt: Date.now(), entries: [] })
      setActivePane("capture")
      report(`In-memory capture started for ${selected().label}.`)
    } else if (action === "stop") {
      setCapture((current) => ({ ...current, active: false }))
      report(`Capture stopped with ${capture().entries.length} line(s).`)
    } else {
      setCapture({ active: false, entries: [] })
      report("In-memory capture cleared.")
    }
  }

  const choosePane = (pane: PaneId) => {
    setActivePane(pane)
    if (pane === "off") {
      jobsOutput = undefined
      return
    }
    setLastPane(pane)
    report(`${pane} pane opened.`)
  }

  const handleWizardAction = (action: WizardAction) => {
    const safeDuringWorkflow = action.startsWith("pane:") || action === "cancel-jobs" || action === "command-mode"
    if (!safeDuringWorkflow && rejectDuringWorkflow("That wizard action")) return
    if (action.startsWith("target:")) return selectBoard(action.slice("target:".length) as BoardId)
    if (action.startsWith("port:")) {
      const port = snapshot().compatiblePorts[Number(action.slice("port:".length))]
      if (port) void connect(activeBoard(), port, false, snapshot()).then((connected) => connected && setWizardStage("home"))
      return
    }
    if (action.startsWith("pane:")) {
      choosePane(action.slice("pane:".length) as PaneId)
      if (workflowBusy()) setWizardStage("running")
      else if (action === "pane:pairing") setWizardStage("pair")
      else if (action !== "pane:overview") setWizardStage("diagnostics")
      return
    }
    if (action === "target-menu") {
      selectionRevision += 1
      setInventoryPending(false)
      return setWizardStage("choose-target")
    }
    if (action === "home") {
      setSnapshot(inspectTarget(root, activeBoard(), ports()))
      setWizardStage("home")
      return
    }
    if (action === "bootstrap-confirm") return setWizardStage("bootstrap-confirm")
    if (action === "build-choice") return setWizardStage("build-choice")
    if (action === "flash-choice") return setWizardStage("flash-choice")
    if (action.startsWith("confirm:")) return askConfirmation(action.slice("confirm:".length) as DestructiveAction)
    if (isDestructive(action)) return runDestructive(action)
    if (action === "pair") return pairingCodes()
    if (action === "diagnostics") return setWizardStage("diagnostics")
    if (action === "choose-port") return setWizardStage("choose-port")
    if (action === "scan") {
      void refreshInventory(true).then((next) => setWizardStage(next.compatiblePorts.length > 1 ? "choose-port" : "home"))
      return
    }
    if (action === "connect") {
      const id = activeBoard()
      const returnToPairing = wizardStage() === "pair" && targets[id].supportsPairing
      void connect(id).then((connected) => {
        if (!connected) setWizardStage("recovery")
        else if (returnToPairing) void pairingCodes(id, true)
        else setWizardStage("home")
      })
      return
    }
    if (action === "codes") return void pairingCodes(activeBoard(), true)
    if (action === "capture") {
      controlCapture("start")
      setWizardStage("diagnostics")
      return
    }
    if (action === "diagnose") return void runDiagnostics()
    if (action === "lab-on") return void controlLab(true)
    if (action === "lab-off") return void controlLab(false)
    if (action === "cancel-jobs") {
      cancelWorkflow()
      return
    }
    if (action === "command-mode") return focusCommand()
    if (action === "bootstrap" || action === "build" || action === "rebuild" || action === "test") {
      void runWorkflow(action)
    }
  }

  const submitPrompt = (value: string) => {
    const command = value.trim()
    if (!command) return
    const normalized = command.toLowerCase()
    recordCommand(command)
    setShowHelp(normalized === "help" || command === "?")
    setPrompt("")
    if (input) input.value = ""

    if (normalized === "wizard") {
      setWizardStage("home")
      setWizardVisible(true)
      queueMicrotask(focusWizard)
      return
    }
    if (normalized === "wizard on") {
      setWizardVisible(true)
      queueMicrotask(focusWizard)
      return
    }
    if (normalized === "wizard off") {
      hideWizard()
      return
    }
    if (normalized === "help" || command === "?") return
    if (normalized === "quit" || normalized === "exit") return quit()
    if (normalized === "terminal clear" || normalized === "clear terminal") {
      clearSerialTerminal()
      return
    }
    if (normalized === "output clear" || normalized === "clear output" || normalized === "clear") {
      clearCommandOutput()
      return
    }
    if (normalized === "cancel") {
      cancelWorkflow()
      return
    }
    if (normalized === "pane on") return choosePane(lastPane())
    const pane = normalized.match(/^pane\s+(overview|jobs|diagnostics|lab|capture|pairing|off)$/)
    if (pane) return choosePane(pane[1] as PaneId)
    const captureAction = normalized.match(/^capture\s+(start|stop|clear)$/)
    if (captureAction) return controlCapture(captureAction[1] as "start" | "stop" | "clear")
    if (normalized === "ports" || normalized === "scan") return void refreshInventory(true)
    if (rejectDuringWorkflow(`Command '${command}'`)) return
    if (normalized === "connect") return void connect().then((connected) => connected && setWizardStage("home"))
    if (normalized === "disconnect") return void disconnect()
    if (normalized === "bootstrap") {
      if (!targets[activeBoard()].commands.bootstrap) {
        report(`${targets[activeBoard()].label} uses the official ESP-IDF setup path; this repo has no automatic installer for it.`, "warning")
        setActivePane("overview")
        setRecovery("Install or configure the ESP toolchain shown in the overview pane, then return to the wizard.")
        setWizardStage("recovery")
        focusWizard()
        return
      }
      setWizardStage("bootstrap-confirm")
      focusWizard()
      return
    }
    // Typing a destructive command is not itself the confirmation. `send <cmd>`
    // stays the deliberate, documented bypass for anyone who wants one.
    if (normalized === "factoryreset" || normalized === "factory-reset") return askConfirmation("factory-reset")
    if (isDestructive(normalized)) return askConfirmation(normalized)
    if (normalized === "build" || normalized === "rebuild" || normalized === "test") {
      return void runWorkflow(normalized)
    }
    if (normalized === "status") return void send(commands[activeBoard()].find(({ id }) => id === "status")!.command)
    if (normalized === "range") return void send(commands[activeBoard()].find(({ id }) => id === "range")!.command)
    if (normalized === "diagnose") return void runDiagnostics()
    if (normalized === "lab on") return void controlLab(true)
    if (normalized === "lab off") return void controlLab(false)
    if (normalized === "pair") return void pairingCodes()
    if (normalized === "codes") return void pairingCodes(activeBoard(), true)
    const target = normalized.match(/^(?:target|board)\s+(nrf|esp32-lock|esp32-reader|esp32)$/)
    if (target) {
      selectBoard(target[1] === "esp32" ? "esp32-reader" : (target[1] as BoardId))
      report(`${targets[target[1] === "esp32" ? "esp32-reader" : (target[1] as BoardId)].label} selected.`)
      return
    }
    const rawCommand = command.match(/^send\s+(.+)$/i)
    if (rawCommand) return void send(rawCommand[1])
    return void send(command)
  }

  const handleGlobalKey = (key: KeyEvent) => {
    if (key.name === "tab") {
      key.preventDefault()
      key.stopPropagation()
      if (!wizardVisible()) {
        focusCommand()
        return
      }
      if (focusArea() === "wizard") focusCommand()
      else focusWizard()
      return
    }
    if (key.name === "q" && focusArea() === "wizard") {
      key.preventDefault()
      key.stopPropagation()
      hideWizard()
      return
    }
    if (key.name === "left" && focusArea() === "wizard") {
      const action = currentBackAction()
      if (!action) return
      key.preventDefault()
      key.stopPropagation()
      handleWizardAction(action)
      return
    }
    if (key.name === "pageup" || (key.ctrl && key.name === "u")) {
      key.preventDefault()
      if (key.name === "pageup" && key.ctrl && activePane() === "jobs") jobsOutput?.scrollBy(-1, "viewport")
      else if (key.name === "pageup" && key.shift) serialOutput?.scrollBy(-1, "viewport")
      else commandOutput?.scrollBy(-1, "viewport")
      return
    }
    if (key.name === "pagedown" || (key.ctrl && key.name === "d")) {
      key.preventDefault()
      if (key.name === "pagedown" && key.ctrl && activePane() === "jobs") jobsOutput?.scrollBy(1, "viewport")
      else if (key.name === "pagedown" && key.shift) serialOutput?.scrollBy(1, "viewport")
      else commandOutput?.scrollBy(1, "viewport")
      return
    }
    if (key.name === "escape" && focusArea() === "wizard") {
      key.preventDefault()
      focusCommand()
    }
  }

  createEffect(() => {
    currentWizardView()
    queueMicrotask(() => wizardSelect?.setSelectedIndex(0))
  })

  createEffect(() => {
    const pane = activePane()
    if (pane !== "off") setLastPane(pane)
  })

  createEffect(() => {
    const width = dimensions().width
    const pane = activePane()
    const available =
      compact() ? width - 4 : pane === "off" ? Math.floor(width / 2) - 4 : pane === "jobs" ? Math.floor(width / 2) - 3 : width - 47
    const columns = Math.max(40, available)
    for (const id of targetIds) {
      const lines = terminals.get(id)?.resize(columns) ?? []
      setSerialLines((current) => ({ ...current, [id]: lines }))
    }
  })

  onMount(() => {
    const unsubscribe = runner.onChange(setJobs)
    const pulse = setInterval(() => setPulseIndex((index) => (index + 1) % 4), 280)
    renderer.keyInput.on("keypress", handleGlobalKey)
    void refreshInventory(false)
    focusWizard()
    onCleanup(() => {
      disposed = true
      clearInterval(pulse)
      renderer.keyInput.off("keypress", handleGlobalKey)
      runner.cancelAll()
      closeAllTransports()
      for (const terminal of terminals.values()) terminal.dispose()
      unsubscribe()
    })
  })

  return (
    <box style={{ flexDirection: "column", width: "100%", height: "100%", padding: 1 }}>
      <box
        style={{
          flexDirection: "row",
          justifyContent: "space-between",
          height: 1,
          flexShrink: 0,
          marginBottom: short() ? 0 : 1
        }}
      >
        <text style={{ fg: theme.foreground }}>openaliro</text>
        <text style={{ fg: theme.muted }}>{`${["·", "∙", "•", "∙"][pulseIndex()]} ${selected().label} · ${selected().connection}`}</text>
      </box>

      <tab_select
        ref={(value) => (boardTabs = value)}
        options={boardOptions()}
        height={1}
        flexShrink={0}
        width="100%"
        textColor={theme.muted}
        backgroundColor="transparent"
        focusedTextColor={theme.foreground}
        focusedBackgroundColor="transparent"
        selectedTextColor={theme.foreground}
        selectedBackgroundColor="transparent"
        selectedDescriptionColor={theme.muted}
        showDescription={false}
        showUnderline={false}
        onChange={(index) => selectBoard(targetIds[index], false)}
      />

      <box style={{ flexDirection: compact() ? "column" : "row", flexGrow: 1, marginTop: short() ? 0 : 1 }}>
        <box style={{ flexDirection: primaryStacked() ? "column" : "row", flexGrow: 1 }}>
          <box
            style={{
              flexDirection: "column",
              flexGrow: 1,
              width: primaryStacked() ? "100%" : "58%",
              height: primaryStacked() ? "50%" : "100%"
            }}
          >
            <CommandOutput
              activity={activity()}
              showHelp={showHelp()}
              onReady={(value) => (commandOutput = value)}
            />
          </box>
          <box
            style={{
              flexDirection: "column",
              flexGrow: 1,
              width: primaryStacked() ? "100%" : "42%",
              height: primaryStacked() ? "50%" : "100%",
              marginLeft: primaryStacked() ? 0 : 1,
              marginTop: primaryStacked() ? 1 : 0
            }}
          >
            <SerialTerminal
              board={selected()}
              serialLines={serialLines()[activeBoard()]}
              onReady={(value) => (serialOutput = value)}
            />
          </box>
        </box>
        <Show when={activePane() !== "off"}>
          <SidePane
            pane={activePane()}
            board={selected()}
            snapshot={snapshot()}
            jobs={jobs()}
            capture={capture()}
            compact={compact()}
            onJobsReady={(value) => (jobsOutput = value)}
          />
        </Show>
      </box>

      <Show when={wizardVisible()}>
        <WizardCard
          view={currentWizardView()}
          focused={focusArea() === "wizard"}
          short={short()}
          pulse={runningJob() ? ["∙", "•", "∙", "·"][pulseIndex()] : "·"}
          canGoBack={Boolean(currentBackAction())}
          onReady={(value) => (wizardSelect = value)}
          onAction={handleWizardAction}
        />
      </Show>

      <box style={{ flexDirection: "column", height: short() ? 4 : 5, flexShrink: 0, marginTop: 1 }}>
        <box style={{ flexDirection: "row", justifyContent: "space-between" }}>
          <text style={{ fg: theme.muted }}>
            {selected().connection === "connected"
              ? `${selected().label} firmware shell`
              : `${selected().label} · TUI command`}
          </text>
          <text style={{ fg: focusArea() === "command" ? theme.foreground : theme.muted }}>
            {focusArea() === "command"
              ? selected().connection === "connected"
                ? "unknown commands go directly to firmware"
                : "TUI command mode"
              : "Tab to type"}
          </text>
        </box>
        <box
          style={{
            flexDirection: "row",
            alignItems: "center",
            height: 3,
            border: true,
            borderStyle: "single",
            borderColor: focusArea() === "command" ? theme.foreground : theme.line,
            paddingLeft: 1,
            paddingRight: 1
          }}
        >
          <text style={{ fg: theme.muted, marginRight: 1 }}>{">"}</text>
          <input
            ref={(value) => (input = value)}
            value={prompt()}
            placeholder={selected().connection === "connected" ? "firmware command or ? for TUI help" : "connect, wizard, or ? for help"}
            placeholderColor={theme.muted}
            textColor={theme.foreground}
            focusedTextColor={theme.foreground}
            style={{ flexGrow: 1 }}
            focused={focusArea() === "command"}
            onInput={setPrompt}
            on:enter={submitPrompt}
          />
        </box>
        <Show when={!short()}>
          <text style={{ fg: theme.muted }}>
            wizard on|off · ? help · connect · send &lt;command&gt; · pane on|off · terminal clear · build · flash · pair · quit
          </text>
        </Show>
      </box>
    </box>
  )
}
