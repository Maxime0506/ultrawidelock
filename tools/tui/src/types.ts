export type BoardId = "nrf" | "esp32-lock" | "esp32-reader"

export type ConnectionState = "disconnected" | "connecting" | "connected" | "error"

export type Severity = "info" | "success" | "warning" | "error"

export type BoardState = {
  id: BoardId
  label: string
  color: import("@opentui/core").ColorInput
  connection: ConnectionState
  port?: string
  firmware?: string
  lastRangeCm?: number
  trustedRange?: boolean
  lastSeenAt?: number
  responder?: boolean
  diagnostics?: Record<string, boolean>
  counters?: { good: number; error: number; timeout: number; tx: number }
  pairing?: {
    qrContent?: string
    qrUrl?: string
    manualCode?: string
  }
  statusLine: string
  logs: LogEntry[]
  alab: AlabEvent[]
}

export type LogEntry = {
  timestamp: number
  severity: Severity
  text: string
}

export type AlabEvent = {
  timestampUs: number
  name: string
  attributes: Record<string, number>
}

export type JobState = "queued" | "running" | "passed" | "failed" | "cancelled"

export type Job = {
  id: string
  label: string
  command: string[]
  cwd: string
  state: JobState
  startedAt?: number
  endedAt?: number
  output: string[]
}

export type CommandDefinition = {
  id: string
  label: string
  command: string
  kind: "query" | "control" | "diagnostic"
}
