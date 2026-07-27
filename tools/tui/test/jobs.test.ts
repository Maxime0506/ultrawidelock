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

test("reports a nonzero exit as failed and keeps the output that explains it", async () => {
  // The whole recovery wizard stage hangs off this one branch, so it is the
  // difference between a visible failure and a workflow that appears to pass.
  const runner = new JobRunner()
  const job = await runner.run("failing", ["sh", "-c", "echo boom >&2; exit 3"], process.cwd())
  expect(job.state).toBe("failed")
  expect(job.output.join("\n")).toContain("boom")
  expect(job.endedAt).toBeGreaterThan(0)
})

test("reports an unstartable command as failed instead of throwing", async () => {
  const runner = new JobRunner()
  const job = await runner.run("missing", ["openaliro-command-that-does-not-exist"], process.cwd())
  expect(job.state).toBe("failed")
  expect(job.output.length).toBeGreaterThan(0)
})

test("a failed job does not stop the next queued job from running", async () => {
  const runner = new JobRunner()
  const [failed, passed] = await Promise.all([
    runner.run("first", ["sh", "-c", "exit 1"], process.cwd()),
    runner.run("second", ["sh", "-c", "printf ok"], process.cwd())
  ])
  expect(failed.state).toBe("failed")
  expect(passed.state).toBe("passed")
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
