import type { AlabEvent, BoardId, BoardState, CommandDefinition, LogEntry, Severity } from "./types"
import { theme } from "./theme"

const ANSI_ESCAPE = /\u001B\[[0-?]*[ -/]*[@-~]/g
const ALAB_EVENT = /\[ALAB\]\s+t=(\d+)\s+ev=([^\s]+)((?:\s+[^\s=]+=-?\d+)*)/
const ALAB_ATTRIBUTE = /([^\s=]+)=(-?\d+)/g

export function stripAnsi(value: string): string {
  return value.replace(ANSI_ESCAPE, "")
}

export function makeBoardState(id: BoardId): BoardState {
  const labels: Record<BoardId, string> = {
    nrf: "nRF5340 DK",
    "esp32-lock": "ESP32-S3 Matter lock",
    "esp32-reader": "ESP32-S3 reader"
  }
  return {
    id,
    label: labels[id],
    color: id === "nrf" ? theme.nrf : theme.esp32,
    connection: "disconnected",
    statusLine: "Awaiting bench connection",
    diagnostics: {},
    logs: [],
    alab: []
  }
}

function severityFor(line: string): Severity {
  if (/\b(fail|failed|error|panic|fatal)\b/i.test(line)) return "error"
  if (/\b(warn|timeout|untrusted|busy)\b/i.test(line)) return "warning"
  if (/\b(ok|pass|trusted|up|ready|connected)\b/i.test(line)) return "success"
  return "info"
}

function updateNrf(board: BoardState, line: string): void {
  const range = line.match(/(?:distance|range)\s+(\d+)\s+cm/i)
  if (range) board.lastRangeCm = Number(range[1])
  if (/\btrusted\b/i.test(line) && !/untrusted/i.test(line)) board.trustedRange = true
  if (/trusted\s+(?:no|○|false)|untrusted/i.test(line)) board.trustedRange = false
  const counters = line.match(/✓(\d+).*?✗(\d+).*?⧗(\d+).*?tx(\d+)/)
  if (counters) board.counters = { good: +counters[1], error: +counters[2], timeout: +counters[3], tx: +counters[4] }
  const version = line.match(/commit\s+([0-9a-f]{7,40}|unknown)/i)
  if (version) board.firmware = version[1]
  if (/ccc\s+.*bound/i.test(line)) board.statusLine = "CCC session bound"
  if (/chip\s+.*SPI error/i.test(line)) board.statusLine = "Radio SPI needs attention"
}

function updateEsp32(board: BoardState, line: string): void {
  const range = line.match(/(?:last )?range:\s*(\d+)\s*cm/i)
  if (range) board.lastRangeCm = Number(range[1])
  if (/trusted\s*:\s*(\d+)\s*cm/i.test(line)) board.trustedRange = true
  if (/responder\s*:\s*up/i.test(line)) {
    board.responder = true
    board.statusLine = "Responder listening"
  }
  if (/responder\s*:\s*down|aliro-stop: ok/i.test(line)) board.responder = false
  const app = line.match(/^\s*([^\s]+)\s+([^·]+)·\s+esp-idf\s+(.+)$/i)
  if (app) board.firmware = `${app[1]} ${app[2].trim()}`
}

function pairingFromLine(current: BoardState["pairing"], line: string): BoardState["pairing"] {
  const pairing = { ...current }
  const urlText = line.match(/(?:QR code URL:|commis)\s*(https?:\/\/\S+)/i)?.[1]
  if (urlText) {
    pairing.qrUrl = urlText
    try {
      const url = new URL(urlText)
      const encoded = url.searchParams.get("data") ?? decodeURIComponent(url.hash.slice(1))
      const payload = encoded.match(/MT:[^\s&\]]+/)?.[0]
      if (payload) pairing.qrContent = payload
    } catch {
      // Keep the original URL visible even if a firmware log was truncated.
    }
  }
  const payload = line.match(/\bMT:[^\s&\]]+/)?.[0]
  if (payload) pairing.qrContent = decodeURIComponent(payload)
  const manual = line.match(/(?:Manual\s*Pairing\s*Code:\s*\[?|manual=)([\d-]{11,})/i)?.[1]
  if (manual) pairing.manualCode = manual.replaceAll("-", "")
  return Object.keys(pairing).length > 0 ? pairing : current
}

export function ingestLine(board: BoardState, rawLine: string, timestamp = Date.now()): BoardState {
  const text = stripAnsi(rawLine).replace(/\r$/, "").trim()
  if (!text) return board
  const next = structuredClone(board)
  next.lastSeenAt = timestamp
  next.connection = "connected"
  next.statusLine = text.length > 64 ? `${text.slice(0, 61)}…` : text
  if (next.id === "nrf") updateNrf(next, text)
  else updateEsp32(next, text)
  next.pairing = pairingFromLine(next.pairing, text)
  const entry: LogEntry = { timestamp, severity: severityFor(text), text }
  next.logs = [...next.logs.slice(-599), entry]
  const alab = text.match(ALAB_EVENT)
  if (alab) {
    const attributes: AlabEvent["attributes"] = {}
    for (const match of alab[3].matchAll(ALAB_ATTRIBUTE)) attributes[match[1]] = Number(match[2])
    next.alab = [...next.alab.slice(-199), { timestampUs: Number(alab[1]), name: alab[2], attributes }]
  }
  return next
}

export const commands: Record<BoardId, CommandDefinition[]> = {
  nrf: [
    { id: "status", label: "Refresh status", command: "aliro status", kind: "query" },
    { id: "codes", label: "Reprint Matter pairing codes", command: "matter onboardingcodes ble", kind: "query" },
    { id: "rx", label: "On-air RX/TX event tally", command: "aliro rx", kind: "query" },
    { id: "range", label: "Read range", command: "aliro range", kind: "query" },
    { id: "chip", label: "Read DW3110 device ID over SPI", command: "aliro chip", kind: "diagnostic" },
    { id: "selftest", label: "Radio self-test", command: "aliro selftest", kind: "diagnostic" },
    { id: "log", label: "Ranging heartbeat on or off", command: "aliro log on|off", kind: "diagnostic" },
    { id: "frames-on", label: "Enable range stream", command: "aliro frames on", kind: "diagnostic" },
    { id: "frames-off", label: "Disable range stream", command: "aliro frames off", kind: "diagnostic" },
    { id: "cir", label: "CIA/CIR on, off, dump, or probe when built with CIR=1", command: "aliro cir on|off|dump|probe", kind: "diagnostic" },
    { id: "frec", label: "Flight recorder on, off, dump, or clear", command: "aliro frec on|off|dump|clear", kind: "diagnostic" },
    { id: "version", label: "Read firmware commit", command: "aliro version", kind: "query" }
  ],
  "esp32-lock": [
    { id: "status", label: "Lock, fabric, and range status", command: "status", kind: "query" },
    { id: "range", label: "Read range", command: "range", kind: "query" },
    { id: "lock", label: "Drive the bolt to Locked", command: "lock", kind: "control" },
    { id: "unlock", label: "Drive the bolt to Unlocked", command: "unlock", kind: "control" },
    { id: "codes", label: "Reprint pairing codes", command: "codes", kind: "query" },
    { id: "provisioning", label: "Show identity, trust a credential, or clear trust", command: "aliro prov|trust|clear", kind: "diagnostic" },
    { id: "uwbdiag", label: "Raw per-frame UWB trace", command: "uwbdiag on|off", kind: "diagnostic" },
    { id: "lab", label: "Aliro lab trace", command: "lab on|off", kind: "diagnostic" },
    { id: "flight-recorder", label: "Flight recorder on, off, dump, or clear", command: "fr on|off|dump|clear", kind: "diagnostic" },
    { id: "log", label: "Set runtime log level for a tag", command: "log <tag|*> <level>", kind: "diagnostic" },
    { id: "factory-reset", label: "Erase Matter and Aliro state, then reboot", command: "factoryreset", kind: "control" },
    { id: "firmware-help", label: "List firmware shell commands", command: "help", kind: "query" }
  ],
  "esp32-reader": [
    { id: "status", label: "Refresh status", command: "status", kind: "query" },
    { id: "range", label: "Read range", command: "range", kind: "query" },
    { id: "start", label: "Start responder", command: "aliro-start", kind: "control" },
    { id: "stop", label: "Stop responder", command: "aliro-stop", kind: "control" },
    { id: "provisioning", label: "Show reader identity and trust store", command: "aliro-prov", kind: "diagnostic" },
    { id: "trust", label: "Trust the last credential and persist it", command: "aliro-trust", kind: "control" },
    { id: "stepup", label: "Arm or inspect Access Document verification", command: "aliro-stepup arm|status", kind: "diagnostic" },
    { id: "uwbdiag", label: "Raw per-frame UWB trace", command: "uwbdiag on|off", kind: "diagnostic" },
    { id: "lab", label: "Aliro lab trace", command: "lab on|off", kind: "diagnostic" },
    { id: "firmware-help", label: "List firmware shell commands", command: "help", kind: "query" }
  ]
}

export type BoardAdapter = {
  id: BoardId
  discoveryPatterns: RegExp[]
  commands: CommandDefinition[]
  createState: () => BoardState
  ingest: (state: BoardState, line: string) => BoardState
}

export const adapters: Record<BoardId, BoardAdapter> = {
  nrf: { id: "nrf", discoveryPatterns: [/usbmodem/i, /ttyACM/i], commands: commands.nrf, createState: () => makeBoardState("nrf"), ingest: ingestLine },
  "esp32-lock": {
    id: "esp32-lock",
    discoveryPatterns: [/usbmodem/i, /ttyUSB/i, /wchusb/i, /SLAB/i],
    commands: commands["esp32-lock"],
    createState: () => makeBoardState("esp32-lock"),
    ingest: ingestLine
  },
  "esp32-reader": {
    id: "esp32-reader",
    discoveryPatterns: [/usbmodem/i, /ttyUSB/i, /wchusb/i, /SLAB/i],
    commands: commands["esp32-reader"],
    createState: () => makeBoardState("esp32-reader"),
    ingest: ingestLine
  }
}
