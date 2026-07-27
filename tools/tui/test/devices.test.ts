import { expect, test } from "bun:test"
import { commands, ingestLine, makeBoardState, stripAnsi } from "../src/devices"

test("strips ANSI presentation before parsing console state", () => {
  expect(stripAnsi("\u001b[32mrange: 40 cm\u001b[0m")).toBe("range: 40 cm")
})

test("projects the nRF curated shell into normalized bench state", () => {
  let state = makeBoardState("nrf")
  for (const line of ["range   124 cm (blk 18, 43 ms ago) trusted", "rx      ✓47 ✗1 ⧗2 tx49", "commit  c43c97e"]) {
    state = ingestLine(state, line, 1)
  }
  expect(state.lastRangeCm).toBe(124)
  expect(state.trustedRange).toBe(true)
  expect(state.counters).toEqual({ good: 47, error: 1, timeout: 2, tx: 49 })
  expect(state.firmware).toBe("c43c97e")
})

test("projects ESP32 responder and ranging output into normalized bench state", () => {
  let state = makeBoardState("esp32-reader")
  for (const line of ["responder : up", "last range: 176 cm", "trusted   : 159 cm", "range: 148 cm"]) {
    state = ingestLine(state, line, 1)
  }
  expect(state.responder).toBe(true)
  expect(state.lastRangeCm).toBe(148)
  expect(state.trustedRange).toBe(true)
})

test("preserves bounded log history and flags failures", () => {
  let state = makeBoardState("esp32-reader")
  state = ingestLine(state, "aliro-start: FAILED (rc=-1)", 1)
  expect(state.logs.at(-1)).toMatchObject({ severity: "error" })
  for (let index = 0; index < 605; index++) state = ingestLine(state, `line ${index}`, index)
  expect(state.logs).toHaveLength(600)
})

test("extracts structured Aliro Lab events for the live analyzer", () => {
  const state = ingestLine(makeBoardState("esp32-reader"), "[ALAB] t=120000 ev=ph.auth0 proto=2 id=1", 1)
  expect(state.alab).toEqual([{ timestampUs: 120000, name: "ph.auth0", attributes: { proto: 2, id: 1 } }])
})

test("extracts a scannable Matter payload and manual code from firmware output", () => {
  let state = makeBoardState("esp32-lock")
  state = ingestLine(
    state,
    "QR code URL: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K9042C00KA0648G00",
    1
  )
  state = ingestLine(state, "Manual pairing code: [34970112332]", 2)
  expect(state.pairing).toEqual({
    qrUrl: "https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K9042C00KA0648G00",
    qrContent: "MT:Y.K9042C00KA0648G00",
    manualCode: "34970112332"
  })
})

test("extracts nRF Matter shell onboarding output", () => {
  expect(commands.nrf.find(({ id }) => id === "codes")?.command).toBe("matter onboardingcodes ble")
  let state = makeBoardState("nrf")
  state = ingestLine(state, "QRCode:            MT:Y.K9042C00KA0648G00", 1)
  state = ingestLine(state, "ManualPairingCode: 34970112332", 2)
  expect(state.pairing).toEqual({
    qrContent: "MT:Y.K9042C00KA0648G00",
    manualCode: "34970112332"
  })
})
