import { expect, test } from "bun:test"
import { JobRunner } from "../src/jobs"

test("serializes bench jobs and captures their output", async () => {
  const runner = new JobRunner()
  const snapshots: string[] = []
  runner.onChange((jobs) => snapshots.push(jobs.map((job) => job.state).join(",")))
  await Promise.all([
    runner.run("first", ["sh", "-c", "printf first"], process.cwd()),
    runner.run("second", ["sh", "-c", "printf second"], process.cwd())
  ])
  expect(snapshots.some((state) => state.includes("queued"))).toBe(true)
  expect(snapshots.at(-1)).toBe("passed,passed")
})

test("cancels queued work during TUI shutdown", async () => {
  const runner = new JobRunner()
  let state = ""
  runner.onChange((jobs) => {
    state = jobs[0]?.state ?? ""
  })
  const job = runner.run("queued", ["sh", "-c", "sleep 30"], process.cwd())
  runner.cancelAll()
  await job
  expect(state).toBe("cancelled")
})
