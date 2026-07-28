import { expect, test } from "bun:test"
import { testRender } from "@opentui/solid"
import { createSignal } from "solid-js"

test("the source runner applies the Solid transform", async () => {
  let update!: (value: string) => void
  const view = await testRender(() => {
    const [value, setValue] = createSignal("before")
    update = setValue
    return <text>{value()}</text>
  })
  await view.renderOnce()
  expect(view.captureCharFrame()).toContain("before")
  update("after")
  await view.renderOnce()
  expect(view.captureCharFrame()).toContain("after")
  view.renderer.destroy()
})
