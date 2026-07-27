import { expect, test } from "bun:test"
import { compatiblePortPaths, preferredAvailablePort } from "../src/targets"

test("prefers the nRF VCOM1 console over the silent VCOM0 interface", () => {
  const ports = [
    { path: "/dev/cu.usbmodem0001", kind: "nrf" as const, vendorId: "1366" },
    { path: "/dev/cu.usbmodem0003", kind: "nrf" as const, vendorId: "1366" }
  ]

  expect(compatiblePortPaths("nrf", ports)).toEqual([
    "/dev/cu.usbmodem0003",
    "/dev/cu.usbmodem0001"
  ])
})

test("keeps ordinary ESP serial candidates in natural order", () => {
  const ports = [
    { path: "/dev/ttyUSB10", kind: "esp32" as const },
    { path: "/dev/ttyUSB2", kind: "esp32" as const }
  ]

  expect(compatiblePortPaths("esp32-reader", ports)).toEqual([
    "/dev/ttyUSB2",
    "/dev/ttyUSB10"
  ])
})

test("auto-connect chooses the preferred unused console and falls back safely", () => {
  const compatible = ["/dev/console", "/dev/alternate"]
  expect(preferredAvailablePort(compatible, [])).toBe("/dev/console")
  expect(preferredAvailablePort(compatible, ["/dev/console"])).toBe("/dev/alternate")
  expect(preferredAvailablePort(compatible, compatible)).toBeUndefined()
})
