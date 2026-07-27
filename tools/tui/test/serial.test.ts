import { expect, test } from "bun:test"
import { parseMacSerialInventory, tioSerialArgs } from "../src/serial"

test("classifies macOS serial devices by USB vendor for compatibility checks", () => {
  const inventory = parseMacSerialInventory(`
    "idVendor" = 4966
    "IOCalloutDevice" = "/dev/cu.usbmodem001"
    "idVendor" = 6790
    "IOCalloutDevice" = "/dev/cu.wchusbserial110"
  `)
  expect(inventory.get("/dev/cu.usbmodem001")?.kind).toBe("nrf")
  expect(inventory.get("/dev/cu.wchusbserial110")?.kind).toBe("esp32")
})

// Shaped like real `ioreg -l -w0`: the vendor sits on the USB device node and is
// repeated on its interface children, the callouts hang off IOSerialBSDClient
// nodes further down, and unrelated devices follow. Paths are placeholders.
const ioregTree = `
    +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x1000005ac, registered>
          "IOCalloutDevice" = "/dev/cu.wlan-debug"
    +-o J-Link@00111300  <class IOUSBHostDevice, id 0x100107b62, registered>
        |   "idVendor" = 4966
        | |   "idVendor" = 4966
        |   +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x100107b88, registered>
        |         "IOCalloutDevice" = "/dev/cu.usbmodem-vcom1"
        |   +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x100107b89, registered>
        |         "IOCalloutDevice" = "/dev/cu.usbmodem-vcom2"
    +-o Some Adapter@00130000  <class IOUSBHostDevice, id 0x100000b1c, registered>
        |   +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x100000c01, registered>
        |         "IOCalloutDevice" = "/dev/cu.usbserial-unlabelled"
`

test("keeps one USB device's vendor across all of its serial interfaces", () => {
  const inventory = parseMacSerialInventory(ioregTree)
  // Both nRF VCOMs hang off nodes below the idVendor line, not beside it.
  expect(inventory.get("/dev/cu.usbmodem-vcom1")?.kind).toBe("nrf")
  expect(inventory.get("/dev/cu.usbmodem-vcom2")?.kind).toBe("nrf")
})

test("never lets one device's vendor identify the next device's port", () => {
  const inventory = parseMacSerialInventory(ioregTree)
  // Declaring this "nrf" would be worse than admitting ignorance: portMatches()
  // trusts any kind that is not "unknown", so a wrong answer hides a real board.
  expect(inventory.get("/dev/cu.usbserial-unlabelled")?.kind).toBe("unknown")
  expect(inventory.get("/dev/cu.usbserial-unlabelled")?.vendorId).toBeUndefined()
  // A port printed before any USB device at all has nothing to inherit either.
  expect(inventory.get("/dev/cu.wlan-debug")?.kind).toBe("unknown")
})

test("opens a quiet non-reconnecting 8N1 serial session", () => {
  expect(tioSerialArgs("/dev/example", 115200)).toEqual([
    "--mute",
    "--no-reconnect",
    "--baudrate",
    "115200",
    "--databits",
    "8",
    "--stopbits",
    "1",
    "--parity",
    "none",
    "/dev/example"
  ])
})
