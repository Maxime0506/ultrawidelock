import { expect, test } from "bun:test"
import { SerialTerminalBuffer } from "../src/terminal"

test("emulates carriage returns, ANSI erasure, and partial shell prompts", async () => {
  const terminal = new SerialTerminalBuffer(60, 6)
  expect(terminal.lines()).toEqual([])
  await terminal.write("booting\r\nprogress 10%\rprogress 90%")
  await terminal.write("\u001b[2K\rready\r\nopenaliro> ")

  expect(terminal.lines()).toEqual(["booting", "ready", "openaliro> "])
  terminal.dispose()
})

test("clears the emulated screen and scrollback", async () => {
  const terminal = new SerialTerminalBuffer(40, 3, 20)
  await terminal.write("boot\r\nready\r\nopenaliro> ")

  expect(terminal.clear()).toEqual([])
  terminal.dispose()
})

test("retains boot scrollback beyond the visible terminal rows", async () => {
  const terminal = new SerialTerminalBuffer(40, 3, 20)
  await terminal.write("one\r\ntwo\r\nthree\r\nfour\r\nfive")

  expect(terminal.lines()).toEqual(["one", "two", "three", "four", "five"])
  terminal.dispose()
})
