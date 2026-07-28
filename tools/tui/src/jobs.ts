import type { Job } from "./types"

type JobListener = (jobs: Job[]) => void

export class JobRunner {
  private jobs: Job[] = []
  private listeners = new Set<JobListener>()
  private running = new Map<string, ReturnType<typeof Bun.spawn>>()
  private queue = Promise.resolve()

  public onChange(listener: JobListener): () => void {
    this.listeners.add(listener)
    listener(this.jobs)
    return () => this.listeners.delete(listener)
  }

  private publish(): void {
    const snapshot = this.jobs.map((job) => ({ ...job, output: [...job.output] }))
    for (const listener of this.listeners) listener(snapshot)
  }

  private isCancelled(job: Job): boolean {
    return job.state === "cancelled"
  }

  public async run(label: string, command: string[], cwd: string): Promise<Job> {
    const job: Job = { id: crypto.randomUUID(), label, command, cwd, state: "queued", output: [] }
    this.jobs = [job, ...this.jobs].slice(0, 20)
    this.publish()
    this.queue = this.queue.catch(() => undefined).then(() => this.execute(job))
    await this.queue
    return { ...job, output: [...job.output] }
  }

  private async execute(job: Job): Promise<void> {
    if (job.state === "cancelled") return
    job.state = "running"
    job.startedAt = Date.now()
    this.publish()
    let child: ReturnType<typeof Bun.spawn>
    try {
      child = Bun.spawn(job.command, { cwd: job.cwd, stdout: "pipe", stderr: "pipe" })
    } catch (error) {
      job.output = [error instanceof Error ? error.message : "Could not start command"]
      job.state = "failed"
      job.endedAt = Date.now()
      this.publish()
      return
    }
    this.running.set(job.id, child)
    const appendOutput = (lines: string[]) => {
      if (lines.length === 0) return
      job.output = [...job.output, ...lines].slice(-5000)
      this.publish()
    }
    const collect = async (stream: ReadableStream<Uint8Array> | null) => {
      if (!stream) return
      const decoder = new TextDecoder()
      const reader = stream.getReader()
      let pending = ""
      while (true) {
        const { value, done } = await reader.read()
        if (done) break
        pending += decoder.decode(value, { stream: true })
        const lines = pending.split(/\r\n|\n|\r/)
        pending = lines.pop() ?? ""
        appendOutput(lines.filter(Boolean))
      }
      pending += decoder.decode()
      if (pending) appendOutput([pending])
    }
    await Promise.all([
      collect(child.stdout as ReadableStream<Uint8Array> | null),
      collect(child.stderr as ReadableStream<Uint8Array> | null)
    ])
    const code = await child.exited
    if (!this.isCancelled(job)) job.state = code === 0 ? "passed" : "failed"
    job.endedAt = Date.now()
    this.running.delete(job.id)
    this.publish()
  }

  public cancel(id: string): void {
    const child = this.running.get(id)
    const job = this.jobs.find((item) => item.id === id)
    if (!job) return
    if (child) child.kill()
    job.state = "cancelled"
    this.publish()
  }

  public cancelAll(): void {
    for (const job of this.jobs) {
      if (job.state === "queued" || job.state === "running") this.cancel(job.id)
    }
  }
}
