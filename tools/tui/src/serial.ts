import { promises as fs } from "node:fs"

export type SerialEvent =
  | { type: "data"; data: string }
  | { type: "line"; line: string }
  | { type: "error"; error: Error }
  | { type: "close" }

export type SerialPortKind = "nrf" | "esp32" | "unknown"

export type SerialPortInfo = {
  path: string
  kind: SerialPortKind
  vendorId?: string
}

function kindForVendor(vendorId?: string): SerialPortKind {
  const normalized = vendorId?.toLowerCase().replace(/^0x/, "").padStart(4, "0")
  if (normalized === "1366") return "nrf"
  if (normalized === "1a86" || normalized === "303a" || normalized === "10c4") return "esp32"
  return "unknown"
}

// ioreg prints the registry as a tree: "idVendor" sits on the USB device node
// and is repeated on its interface children, while "IOCalloutDevice" sits on an
// IOSerialBSDClient somewhere below. So the vendor has to survive the nodes
// between a device and its ports, but must not survive past the device itself --
// otherwise a port with no idVendor in its own subtree inherits whatever device
// was printed before it, and a confident wrong `kind` is worse than "unknown":
// portMatches() trusts anything that is not "unknown" and would drop a board
// that is plugged in. Entering the next USB device is the boundary that ends it.
const IOREG_USB_DEVICE = /\+-o .*<class IOUSBHostDevice|\+-o .*<class IOUSBDevice/

export function parseMacSerialInventory(output: string): Map<string, SerialPortInfo> {
  const inventory = new Map<string, SerialPortInfo>()
  let vendorId: string | undefined
  for (const line of output.split(/\r?\n/)) {
    if (IOREG_USB_DEVICE.test(line)) vendorId = undefined
    const vendor = line.match(/"idVendor"\s*=\s*(0x[0-9a-f]+|\d+)/i)?.[1]
    if (vendor) vendorId = vendor.startsWith("0x") ? vendor : Number(vendor).toString(16)
    const port = line.match(/"IOCalloutDevice"\s*=\s*"([^"]+)"/)?.[1]
    if (port) inventory.set(port, { path: port, vendorId, kind: kindForVendor(vendorId) })
  }
  return inventory
}

function fallbackKind(path: string): SerialPortKind {
  if (/wchusb|SLAB|ttyUSB|usbserial/i.test(path)) return "esp32"
  if (/ttyACM/i.test(path)) return "nrf"
  return "unknown"
}

async function probeOutput(command: string[]): Promise<string> {
  try {
    const child = Bun.spawn(command, { stdout: "pipe", stderr: "ignore" })
    const output = await new Response(child.stdout).text()
    await child.exited
    return output
  } catch {
    return ""
  }
}

export async function discoverSerialPortInfo(): Promise<SerialPortInfo[]> {
  const dev = "/dev"
  const entries = await fs.readdir(dev).catch(() => [])
  const pattern = process.platform === "darwin" ? /^(cu\.(usbmodem|SLAB|wchusb|usbserial))/i : /^(ttyACM|ttyUSB)/i
  const paths = entries.filter((entry) => pattern.test(entry)).map((entry) => `${dev}/${entry}`).sort()
  if (process.platform === "darwin") {
    const inventory = parseMacSerialInventory(await probeOutput(["ioreg", "-l", "-w0"]))
    return paths.map((path) => inventory.get(path) ?? { path, kind: fallbackKind(path) })
  }
  return Promise.all(
    paths.map(async (path) => {
      const properties = await probeOutput(["udevadm", "info", "--query=property", `--name=${path}`])
      const vendorId = properties.match(/^ID_VENDOR_ID=(.+)$/m)?.[1]
      return { path, vendorId, kind: kindForVendor(vendorId) === "unknown" ? fallbackKind(path) : kindForVendor(vendorId) }
    })
  )
}

export async function discoverSerialPorts(): Promise<string[]> {
  return (await discoverSerialPortInfo()).map(({ path }) => path)
}

export function tioSerialArgs(path: string, baudRate: number): string[] {
  return ["--mute", "--no-reconnect", "--baudrate", String(baudRate), "--databits", "8", "--stopbits", "1", "--parity", "none", path]
}

export class PosixSerialTransport {
  private child?: ReturnType<typeof Bun.spawn>
  private terminal?: Bun.Terminal
  private buffer = ""
  private decoder = new TextDecoder()
  private closing = false
  private listeners = new Set<(event: SerialEvent) => void>()

  public constructor(
    public readonly path: string,
    public readonly baudRate = 115200
  ) {}

  public on(listener: (event: SerialEvent) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: SerialEvent): void {
    for (const listener of this.listeners) listener(event)
  }

  public async open(): Promise<void> {
    if (this.child) return
    const executable = Bun.which("tio")
    if (!executable) {
      throw new Error(
        process.platform === "darwin"
          ? "Live serial needs tio. Install it with `brew install tio`, then retry connect."
          : "Live serial needs tio. Install the `tio` package, then retry connect."
      )
    }

    this.closing = false
    const terminal = new Bun.Terminal({
      cols: 100,
      rows: 30,
      data: (_terminal, data) => this.read(this.decoder.decode(data, { stream: true }))
    })
    terminal.setRawMode(true)
    const child = Bun.spawn([executable, ...tioSerialArgs(this.path, this.baudRate)], { terminal })
    this.terminal = terminal
    this.child = child
    void child.exited.then((code) => {
      if (this.child !== child) return
      this.child = undefined
      this.terminal = undefined
      terminal.close()
      if (!this.closing && code !== 0) this.emit({ type: "error", error: new Error(`Serial session exited with status ${code}`) })
      if (!this.closing) this.emit({ type: "close" })
    })

    await new Promise((resolve) => setTimeout(resolve, 100))
    if (this.child !== child) throw new Error(`Could not open serial console ${this.path}`)
  }

  private read(chunk: string): void {
    this.emit({ type: "data", data: chunk })
    this.buffer += chunk
    const lines = this.buffer.split(/\n/)
    this.buffer = lines.pop() ?? ""
    for (const line of lines) this.emit({ type: "line", line })
  }

  public async write(command: string): Promise<void> {
    if (!this.terminal || !this.child) throw new Error(`Serial port ${this.path} is not connected`)
    if (this.terminal.write(`${command}\r`) === 0) throw new Error(`Could not write to serial port ${this.path}`)
  }

  public async close(): Promise<void> {
    this.closing = true
    const child = this.child
    const terminal = this.terminal
    this.child = undefined
    this.terminal = undefined
    child?.kill()
    terminal?.close()
    await child?.exited.catch(() => undefined)
    this.emit({ type: "close" })
  }
}
