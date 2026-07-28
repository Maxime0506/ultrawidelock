import { targets, type TargetSnapshot } from "./targets"
import type { BoardId, ConnectionState, JobState } from "./types"

export type WizardStage =
  | "choose-target"
  | "home"
  | "bootstrap-confirm"
  | "build-choice"
  | "flash-choice"
  | "confirm"
  | "pair"
  | "choose-port"
  | "diagnostics"
  | "running"
  | "recovery"

// Every action that programs the board or destroys state it holds. Each one is
// reachable only through the shared `confirm` stage below, from the wizard and
// from the command prompt alike, so there is no path that skips the warning.
export type DestructiveAction = "flash" | "flash-erase" | "rebuild-flash-erase" | "factory-reset"

export const destructiveActions: DestructiveAction[] = ["flash", "flash-erase", "rebuild-flash-erase", "factory-reset"]

export function isDestructive(value: string): value is DestructiveAction {
  return (destructiveActions as string[]).includes(value)
}

export type WizardAction =
  | `target:${BoardId}`
  | "bootstrap-confirm"
  | "bootstrap"
  | "build-choice"
  | "build"
  | "rebuild"
  | "flash-choice"
  | `confirm:${DestructiveAction}`
  | DestructiveAction
  | "test"
  | "scan"
  | "connect"
  | "pair"
  | `port:${number}`
  | "choose-port"
  | "codes"
  | "diagnostics"
  | "diagnose"
  | "lab-on"
  | "lab-off"
  | "capture"
  | "pane:overview"
  | "pane:jobs"
  | "pane:diagnostics"
  | "pane:lab"
  | "pane:capture"
  | "pane:pairing"
  | "target-menu"
  | "home"
  | "cancel-jobs"
  | "command-mode"

export type WizardChoice = {
  name: string
  description: string
  value: WizardAction
  danger?: boolean
}

export type WizardContext = {
  target: BoardId
  snapshot: TargetSnapshot
  connection: ConnectionState
  jobState?: JobState
  inventoryPending?: boolean
  pairingReady: boolean
  recovery?: string
  pending?: DestructiveAction
}

type ConfirmSpec = {
  title: string
  detail: string
  confirmName: string
  confirmDescription: string
  /** Stage to return to when the confirmation is declined or Left Arrow is pressed. */
  back: WizardAction
}

// One shape for every destructive confirmation: same eyebrow, same "go back
// first" ordering, same single danger-marked accept. The wording differs only
// where the loss differs, so the screen is recognisable before it is read.
const confirmSpecs: Record<DestructiveAction, ConfirmSpec> = {
  flash: {
    title: "Program the board with this build?",
    detail:
      "The running firmware is replaced and the board reboots. Stored commissioning is preserved where the target supports it, but the serial session is dropped while the flashing tool owns the device.",
    confirmName: "Flash the current build",
    confirmDescription: "Replace the running firmware and reboot the board.",
    back: "flash-choice"
  },
  "flash-erase": {
    title: "Erase all persistent board state, then flash?",
    detail:
      "This removes Matter commissioning, Aliro provisioning, and trusted credentials. Remove the stale accessory from the home before pairing again.",
    confirmName: "Erase and flash the current build",
    confirmDescription: "I understand that I may need to commission and pair again.",
    back: "flash-choice"
  },
  "rebuild-flash-erase": {
    title: "Rebuild from scratch, erase all board state, then flash?",
    detail:
      "The build directory is discarded and reconfigured, then Matter commissioning, Aliro provisioning, and trusted credentials are erased. This is the longest and least reversible path.",
    confirmName: "Rebuild, erase, and flash",
    confirmDescription: "I understand that this discards both the build and every stored credential.",
    back: "flash-choice"
  },
  "factory-reset": {
    title: "Factory reset the connected board?",
    detail:
      "The firmware erases its Matter fabrics, its Aliro reader identity, and every trusted phone key, then reboots unprovisioned. Nothing is written to the board's flash image, so no rebuild is needed to recover.",
    confirmName: "Erase every credential and reboot",
    confirmDescription: "I understand that the home will need this accessory removed and paired again.",
    back: "diagnostics"
  }
}

export function confirmView(action: DestructiveAction): WizardView {
  const spec = confirmSpecs[action]
  return {
    eyebrow: "destructive confirmation",
    title: spec.title,
    detail: spec.detail,
    danger: true,
    choices: [
      { name: "Go back", description: "Nothing is changed on the board or in this clone.", value: spec.back },
      { name: spec.confirmName, description: spec.confirmDescription, value: action, danger: true }
    ]
  }
}

export type WizardView = {
  eyebrow: string
  title: string
  detail: string
  choices: WizardChoice[]
  /** Draw this screen in the terminal's danger colour: it is a point of no return. */
  danger?: boolean
}

export function wizardBackAction(stage: WizardStage, context: WizardContext): WizardAction | undefined {
  if (context.jobState === "queued" || context.jobState === "running") return undefined

  switch (stage) {
    case "home":
      return "target-menu"
    case "confirm":
      // Left Arrow must land where the confirmation was reached from, never on
      // the accept, so the escape is always the same key as everywhere else.
      return context.pending ? confirmSpecs[context.pending].back : "home"
    case "bootstrap-confirm":
    case "build-choice":
    case "flash-choice":
    case "pair":
    case "choose-port":
    case "diagnostics":
    case "recovery":
      return "home"
    case "choose-target":
    case "running":
      return undefined
  }
}

export function targetChoices(): WizardChoice[] {
  return (Object.keys(targets) as BoardId[]).map((id) => ({
    name: targets[id].label,
    description: targets[id].description,
    value: `target:${id}`
  }))
}

function homeChoices(context: WizardContext): WizardChoice[] {
  const { snapshot, connection, target } = context
  const spec = targets[target]
  const choices: WizardChoice[] = []

  if (!snapshot.setupReady && spec.commands.bootstrap) {
    choices.push({
      name: "Bootstrap this clone",
      description: "Install missing host tools, NCS 3.3.0, and the ~6.5 GB workspace.",
      value: "bootstrap-confirm"
    })
  }
  if (!snapshot.setupReady && !spec.commands.bootstrap) {
    choices.push({
      name: "Show the setup path",
      description: `${spec.setupGuide} · this repo does not install ESP-IDF automatically.`,
      value: "pane:overview"
    })
  }

  if (snapshot.setupReady) {
    choices.push({
      name: snapshot.artifact === "missing" ? "Build firmware" : "Review build choices",
      description:
        snapshot.artifact === "missing"
          ? `Create ${snapshot.artifactPath}.`
          : `${snapshot.artifactPath} is ${snapshot.artifact.replaceAll("-", " ")}.`,
      value: "build-choice"
    })
  }

  if (snapshot.compatiblePorts.length === 0) {
    choices.push({ name: "Look for a board", description: "Rescan serial devices and explain incompatibilities.", value: "scan" })
  } else if (connection !== "connected") {
    choices.push({
      name: snapshot.compatiblePorts.length > 1 ? "Choose a serial port" : "Connect to the board",
      description: `${snapshot.compatiblePorts.length} compatible serial port${snapshot.compatiblePorts.length === 1 ? "" : "s"} found.`,
      value: snapshot.compatiblePorts.length > 1 ? "choose-port" : "connect"
    })
  } else {
    choices.push({ name: "Open live diagnostics", description: "Status, range, lab, captures, and jobs.", value: "diagnostics" })
  }

  if (snapshot.artifact === "current" && snapshot.compatiblePorts.length > 0) {
    choices.push({ name: "Flash firmware", description: "Choose a normal flash or a confirmed full erase.", value: "flash-choice" })
  }
  if (spec.supportsPairing) {
    choices.push({
      name: context.pairingReady ? "Show pairing QR" : "Pair this lock",
      description: context.pairingReady ? "Open the captured onboarding code." : "Connect and collect the firmware onboarding code.",
      value: "pair"
    })
  }
  choices.push({ name: "Run host tests", description: "Check core logic without hardware or firmware tools.", value: "test" })
  choices.push({ name: "Choose another target", description: "nRF DK, ESP Matter lock, or standalone ESP reader.", value: "target-menu" })
  return choices
}

export function wizardView(stage: WizardStage, context: WizardContext): WizardView {
  const spec = targets[context.target]
  if (context.jobState === "queued" || context.jobState === "running") {
    stage = "running"
  }
  if (context.inventoryPending && stage !== "running") {
    return {
      eyebrow: "checking bench",
      title: `Finding ${spec.label}`,
      detail: "Inspecting serial devices and choosing the preferred unused console. The rest of the interface remains available.",
      choices: [
        { name: "Back to target selection", description: "Stop waiting for this target and choose another one.", value: "target-menu" },
        { name: "Use the command line", description: "Keep the scan running while you use the fixed prompt.", value: "command-mode" }
      ]
    }
  }
  if (stage === "choose-target") {
    return {
      eyebrow: "guided setup",
      title: "What do you want to put on the bench?",
      detail: "Choose a real firmware target. The wizard will inspect this clone before offering the next step.",
      choices: targetChoices()
    }
  }
  if (stage === "bootstrap-confirm") {
    return {
      eyebrow: "download confirmation",
      title: "Bootstrap the nRF development environment?",
      detail: "This installs missing host packages, the NCS 3.3.0 toolchain (~2 GB), and a patched workspace (~6.5 GB). It is resumable.",
      choices: [
        { name: "Install and fetch", description: "Run the repository's make bootstrap path with its tool prompt pre-approved.", value: "bootstrap" },
        { name: "Not now", description: "Return without changing the machine.", value: "home" }
      ]
    }
  }
  if (stage === "build-choice") {
    return {
      eyebrow: "build",
      title: context.snapshot.artifact === "missing" ? `Build ${spec.label}` : "Use the existing build or rebuild?",
      detail: `${context.snapshot.artifactPath} is ${context.snapshot.artifact.replaceAll("-", " ")}. Incremental builds still recompile changed source.`,
      choices: [
        { name: "Incremental build", description: "Fast path; the repo build detects configuration changes.", value: "build" },
        { name: "Pristine rebuild", description: "Discard target build intermediates and configure from scratch.", value: "rebuild" },
        { name: "Keep the existing build", description: "Return to the next valid bench actions.", value: "home" }
      ]
    }
  }
  if (stage === "flash-choice") {
    return {
      eyebrow: "flash",
      title: "How should the board be programmed?",
      detail: "The serial connection is released before programming so the flashing tool owns the device. Every option here confirms before it touches the board.",
      choices: [
        { name: "Normal flash", description: "Write the current application while preserving stored commissioning where supported.", value: "confirm:flash" },
        {
          name: "Full erase and flash",
          description: "Erase persistent state first. Pairing and trusted credentials are lost.",
          value: "confirm:flash-erase",
          danger: true
        },
        {
          name: "Pristine rebuild, erase, and flash",
          description: "Rebuild the current repository state from scratch, erase persistent state, then program it.",
          value: "confirm:rebuild-flash-erase",
          danger: true
        },
        { name: "Go back", description: "Do not program the board.", value: "home" }
      ]
    }
  }
  if (stage === "confirm") {
    return confirmView(context.pending ?? "flash")
  }
  if (stage === "pair") {
    return {
      eyebrow: "commission and pair",
      title: context.pairingReady ? "The onboarding code is ready" : "Collect the onboarding code from the board",
      detail: context.pairingReady
        ? "The Pairing pane contains the scannable QR payload and manual code."
        : context.target === "esp32-lock"
          ? "Connect, then ask the firmware to reprint its commissioning codes."
          : "Connect, then ask the Matter shell to print its QR payload and manual pairing code.",
      choices: [
        ...(context.pairingReady
          ? [{ name: "Show pairing pane", description: "Keep the QR visible while you commission from the phone.", value: "pane:pairing" as const }]
          : context.connection === "connected"
            ? [{ name: "Request onboarding codes", description: "Ask the connected firmware to print a fresh QR payload and manual code.", value: "codes" as const }]
            : [{ name: "Connect and request codes", description: "Open serial, then request the onboarding data automatically.", value: "connect" as const }]),
        { name: "Back to setup", description: "Return to the guided bench loop.", value: "home" }
      ]
    }
  }
  if (stage === "choose-port") {
    return {
      eyebrow: "serial device",
      title: "Which attached board should this session use?",
      detail: "Compatibility uses USB vendor identity when available, with a conservative device-name fallback. Flash tooling performs its own safety check as well.",
      choices: [
        ...context.snapshot.compatiblePorts.map((port, index) => ({
          name: `${port.split("/").at(-1) ?? port}${context.target === "nrf" && index === 0 ? " · console" : ""}`,
          description:
            context.target === "nrf" && index === 0
              ? `${port} · recommended VCOM1 firmware console`
              : context.target === "nrf"
                ? `${port} · alternate J-Link interface`
                : port,
          value: `port:${index}` as const
        })),
        { name: "Scan again", description: "Refresh the device inventory.", value: "scan" },
        { name: "Go back", description: "Return without opening a port.", value: "home" }
      ]
    }
  }
  if (stage === "diagnostics") {
    return {
      eyebrow: "live workspace",
      title: "Which view should stay beside the output?",
      detail: "The prompt, wizard, header, and selected pane stay fixed while the main output scrolls.",
      choices: [
        { name: "Run a diagnostic sweep", description: "Connect if needed, then collect read-only status, range, radio, and provisioning facts.", value: "diagnose" },
        { name: "Board diagnostics", description: "Connection, firmware, range, trust, and counters.", value: "pane:diagnostics" },
        ...(context.target === "nrf"
          ? []
          : [
              { name: "Start Aliro Lab", description: "Enable structured transaction tracing and keep its pane visible.", value: "lab-on" as const },
              { name: "Stop Aliro Lab", description: "Disable transaction tracing to remove its timing overhead.", value: "lab-off" as const }
            ]),
        { name: "Live capture", description: "Collect an in-memory serial trace for this session.", value: "capture" },
        { name: "Build and flash jobs", description: "Queued work, command, status, and recent output.", value: "pane:jobs" },
        ...(spec.supportsFactoryReset
          ? [
              {
                name: "Factory reset the board",
                description: "Ask the connected firmware to erase every credential and reboot. Confirmed first.",
                value: "confirm:factory-reset" as const,
                danger: true
              }
            ]
          : []),
        { name: "Back to setup", description: "Return to the guided bench loop.", value: "home" }
      ]
    }
  }
  if (stage === "running") {
    return {
      eyebrow: "working",
      title: `${spec.label} task is running`,
      detail: "Output streams above. You can scroll it, switch panes, or use the command prompt while the task continues.",
      choices: [
        { name: "Show job pane", description: "Follow the active command and its latest output.", value: "pane:jobs" },
        { name: "Cancel active jobs", description: "Stop running and queued work.", value: "cancel-jobs", danger: true },
        { name: "Use the command line", description: "Move focus to the expert prompt.", value: "command-mode" }
      ]
    }
  }
  if (stage === "recovery") {
    return {
      eyebrow: "recovery",
      title: "That step did not complete",
      detail: context.recovery ?? "The command output above has the exact failure.",
      choices: [
        ...(!context.snapshot.setupReady && targets[context.target].commands.bootstrap
          ? [{ name: "Bootstrap prerequisites", description: "Repair the environment before retrying.", value: "bootstrap-confirm" as const }]
          : []),
        { name: "Inspect current state", description: "Rescan tools, artifacts, and ports, then offer valid actions.", value: "home" },
        { name: "Run host tests instead", description: "Keep making progress without firmware tools or hardware.", value: "test" },
        { name: "Open job output", description: "Keep the failed command visible in a side pane.", value: "pane:jobs" }
      ]
    }
  }
  return {
    eyebrow: "guided setup",
    title: `${spec.label} is ready for the next step`,
    detail: `${context.snapshot.setupDetail}. Build: ${context.snapshot.artifact.replaceAll("-", " ")}. Ports: ${context.snapshot.compatiblePorts.length}.`,
    choices: homeChoices(context)
  }
}
