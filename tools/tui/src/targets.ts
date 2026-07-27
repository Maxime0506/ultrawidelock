import { existsSync, readdirSync, statSync } from "node:fs"
import { homedir } from "node:os"
import { dirname, isAbsolute, join, resolve } from "node:path"
import type { SerialPortInfo } from "./serial"
import type { BoardId } from "./types"

export type TargetSpec = {
  id: BoardId
  label: string
  shortLabel: string
  description: string
  artifact: string
  setupGuide: string
  supportsPairing: boolean
  /** The firmware exposes a console command that erases its own stored credentials. */
  supportsFactoryReset: boolean
  commands: {
    bootstrap?: string[]
    build: string[]
    rebuild: string[]
    test: string[]
    flash: string[]
    flashErase: string[]
  }
}

export type TargetSnapshot = {
  target: BoardId
  setupReady: boolean
  setupDetail: string
  artifact: "missing" | "current" | "older-than-source"
  artifactPath: string
  compatiblePorts: string[]
  incompatiblePorts: string[]
}

export const targetIds: BoardId[] = ["nrf", "esp32-lock", "esp32-reader"]

export const targets: Record<BoardId, TargetSpec> = {
  nrf: {
    id: "nrf",
    label: "nRF5340 DK",
    shortLabel: "nRF DK",
    description: "Primary Matter lock with NFC and UWB.",
    artifact: "build/merged.hex",
    setupGuide: "docs/set-up.md#nrf5340-dk-primary-target",
    supportsPairing: true,
    supportsFactoryReset: true,
    commands: {
      bootstrap: ["make", "bootstrap", "ASSUME_YES=1"],
      build: ["make", "build"],
      rebuild: ["make", "rebuild"],
      test: ["make", "test"],
      flash: ["make", "flash"],
      flashErase: ["make", "flash-erase"]
    }
  },
  "esp32-lock": {
    id: "esp32-lock",
    label: "ESP32-S3 Matter lock",
    shortLabel: "ESP lock",
    description: "Matter lock with onboarding, Wallet key, and UWB.",
    artifact: "ports/esp32/apps/matter-lock/build/door_lock.bin",
    setupGuide: "ports/esp32/apps/matter-lock/README.md#prerequisites",
    supportsPairing: true,
    supportsFactoryReset: true,
    commands: {
      build: ["make", "-C", "ports/esp32/apps/matter-lock", "build"],
      rebuild: ["make", "-C", "ports/esp32/apps/matter-lock", "rebuild"],
      test: ["make", "test-port"],
      flash: ["make", "-C", "ports/esp32/apps/matter-lock", "flash"],
      flashErase: ["make", "-C", "ports/esp32/apps/matter-lock", "flash-erase"]
    }
  },
  "esp32-reader": {
    id: "esp32-reader",
    label: "ESP32-S3 reader",
    shortLabel: "ESP reader",
    description: "Standalone Aliro reader and diagnostic responder.",
    artifact: "ports/esp32/apps/reader/build/woz_uwb_esp32s3.bin",
    setupGuide: "docs/set-up.md#esp32-s3-ports",
    supportsPairing: false,
    supportsFactoryReset: false,
    commands: {
      build: ["make", "-C", "ports/esp32/apps/reader", "build"],
      rebuild: ["make", "-C", "ports/esp32/apps/reader", "clean", "build"],
      test: ["make", "test-port"],
      flash: ["make", "-C", "ports/esp32/apps/reader", "flash"],
      flashErase: ["make", "-C", "ports/esp32/apps/reader", "flash-erase"]
    }
  }
}

const ignoredSourceDirectories = new Set([".git", "build", "dist", "node_modules", "site", "workspace"])

function newestMtime(path: string): number {
  if (!existsSync(path)) return 0
  const stat = statSync(path)
  if (!stat.isDirectory()) return stat.mtimeMs
  let newest = stat.mtimeMs
  for (const entry of readdirSync(path, { withFileTypes: true })) {
    if (entry.isDirectory() && (ignoredSourceDirectories.has(entry.name) || entry.name.startsWith("build-"))) continue
    newest = Math.max(newest, newestMtime(join(path, entry.name)))
  }
  return newest
}

function sourcePaths(root: string, target: BoardId): string[] {
  const shared = [join(root, "modules"), join(root, "deps"), join(root, "Makefile")]
  if (target === "nrf") return [...shared, join(root, "ports/nrf5340dk"), join(root, "scripts/build.sh")]
  const app = target === "esp32-lock" ? "matter-lock" : "reader"
  return [...shared, join(root, "ports/esp32/components"), join(root, `ports/esp32/apps/${app}`)]
}

function setupState(root: string, target: BoardId): Pick<TargetSnapshot, "setupReady" | "setupDetail"> {
  if (target === "nrf") {
    const localReady = existsSync(join(root, "workspace/.west"))
    const commonProbe = Bun.spawnSync(["git", "-C", root, "rev-parse", "--git-common-dir"], { stdout: "pipe", stderr: "ignore" })
    const commonText = new TextDecoder().decode(commonProbe.stdout).trim()
    const commonDirectory = commonText ? (isAbsolute(commonText) ? commonText : resolve(root, commonText)) : ""
    const primaryReady = Boolean(commonDirectory) && existsSync(join(dirname(commonDirectory), "workspace/.west"))
    const workspaceReady = localReady || primaryReady
    const toolchainReady =
      process.env.ALIRO_TOOLCHAIN === "env" ? Boolean(Bun.which("west")) : Boolean(Bun.which("nrfutil"))
    const ready = workspaceReady && toolchainReady
    return {
      setupReady: ready,
      setupDetail: !workspaceReady
        ? "NCS toolchain and workspace need bootstrap"
        : !toolchainReady
          ? process.env.ALIRO_TOOLCHAIN === "env"
            ? "NCS workspace is present, but west is not on PATH"
            : "NCS workspace is present, but nrfutil is not on PATH"
          : localReady
            ? "NCS workspace and toolchain are present"
            : "Primary NCS workspace is present; this worktree can seed itself on first build"
    }
  }

  const idfExport =
    process.env.IDF_EXPORT ??
    (process.env.IDF_PATH ? join(process.env.IDF_PATH, "export.sh") : join(homedir(), "esp/esp-idf/export.sh"))
  const idfReady = existsSync(idfExport)
  if (target === "esp32-reader") {
    return {
      setupReady: idfReady,
      setupDetail: idfReady ? "ESP-IDF export is present" : "ESP-IDF is not available at the configured path"
    }
  }

  const matterPath = process.env.ESP_MATTER_PATH ?? join(homedir(), "esp/esp-matter")
  const matterReady = existsSync(join(matterPath, "export.sh"))
  return {
    setupReady: idfReady && matterReady,
    setupDetail:
      idfReady && matterReady
        ? "ESP-IDF and esp-matter exports are present"
        : "ESP-IDF and esp-matter must be installed or configured"
  }
}

function portMatches(target: BoardId, port: SerialPortInfo): boolean {
  if (port.kind !== "unknown") return target === "nrf" ? port.kind === "nrf" : port.kind === "esp32"
  if (target === "nrf") return /usbmodem|ttyACM/i.test(port.path)
  return /usbmodem|ttyUSB|wchusb|SLAB|usbserial/i.test(port.path)
}

export function compatiblePortPaths(target: BoardId, ports: SerialPortInfo[]): string[] {
  const compatible = ports.filter((port) => portMatches(target, port)).map(({ path }) => path)
  return compatible.sort((left, right) =>
    target === "nrf"
      ? right.localeCompare(left, undefined, { numeric: true })
      : left.localeCompare(right, undefined, { numeric: true })
  )
}

export function preferredAvailablePort(compatiblePorts: string[], usedPorts: Iterable<string>): string | undefined {
  const used = new Set(usedPorts)
  return compatiblePorts.find((port) => !used.has(port))
}

export function inspectTarget(root: string, target: BoardId, ports: SerialPortInfo[]): TargetSnapshot {
  const spec = targets[target]
  const artifactPath = join(root, spec.artifact)
  const artifactMtime = existsSync(artifactPath) ? statSync(artifactPath).mtimeMs : 0
  const sourceMtime = Math.max(...sourcePaths(root, target).map(newestMtime))
  const artifact =
    artifactMtime === 0 ? "missing" : artifactMtime >= sourceMtime ? "current" : "older-than-source"
  const compatiblePorts = compatiblePortPaths(target, ports)
  return {
    target,
    ...setupState(root, target),
    artifact,
    artifactPath: spec.artifact,
    compatiblePorts,
    incompatiblePorts: ports.filter((port) => !compatiblePorts.includes(port.path)).map(({ path }) => path)
  }
}
