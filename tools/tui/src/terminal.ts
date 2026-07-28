import { Terminal } from "@xterm/headless"

const DEFAULT_COLUMNS = 100
const DEFAULT_ROWS = 24
const DEFAULT_SCROLLBACK = 4000

export class SerialTerminalBuffer {
  private readonly terminal: Terminal

  public constructor(columns = DEFAULT_COLUMNS, rows = DEFAULT_ROWS, scrollback = DEFAULT_SCROLLBACK) {
    this.terminal = new Terminal({
      allowProposedApi: true,
      cols: Math.max(2, columns),
      rows: Math.max(1, rows),
      scrollback,
      convertEol: true
    })
  }

  public write(data: string | Uint8Array): Promise<string[]> {
    return new Promise((resolve) => {
      this.terminal.write(data, () => resolve(this.lines()))
    })
  }

  public resize(columns: number, rows = DEFAULT_ROWS): string[] {
    const nextColumns = Math.max(2, columns)
    const nextRows = Math.max(1, rows)
    if (this.terminal.cols !== nextColumns || this.terminal.rows !== nextRows) {
      this.terminal.resize(nextColumns, nextRows)
    }
    return this.lines()
  }

  public lines(): string[] {
    const buffer = this.terminal.buffer.active
    const lines = Array.from({ length: buffer.length }, (_, index) => buffer.getLine(index)?.translateToString(true) ?? "")
    const lastContent = lines.findLastIndex((line) => line.length > 0)
    if (lastContent < 0 && buffer.cursorX === 0) return []
    const cursorLine = buffer.baseY + buffer.cursorY
    const lastVisible = Math.max(lastContent, buffer.cursorX > 0 ? cursorLine : -1)
    return lines.slice(0, lastVisible + 1)
  }

  public clear(): string[] {
    this.terminal.reset()
    return this.lines()
  }

  public dispose(): void {
    this.terminal.dispose()
  }
}
