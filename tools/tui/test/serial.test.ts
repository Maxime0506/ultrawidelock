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
