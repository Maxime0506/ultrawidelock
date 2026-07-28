import solidPlugin from "@opentui/solid/bun-plugin"

const root = new URL("..", import.meta.url)
const dist = new URL("../dist/", import.meta.url)
const release = Bun.argv.includes("--release")
const requested = Bun.argv.find((argument) => argument.startsWith("--target="))?.slice("--target=".length)
const targets: Array<Bun.Build.CompileTarget | undefined> = requested
  ? [requested as Bun.Build.CompileTarget]
  : release
    ? ["bun-darwin-arm64", "bun-linux-x64"]
    : [undefined]
const git = Bun.spawn(["git", "rev-parse", "--short", "HEAD"], { cwd: new URL("../../..", import.meta.url).pathname, stdout: "pipe", stderr: "ignore" })
const revision = (await new Response(git.stdout).text()).trim() || "source"

await Bun.write(new URL(".gitkeep", dist), "")

for (const target of targets) {
  const suffix = target?.replace("bun-", "") ?? "local"
  const result = await Bun.build({
    entrypoints: [new URL("../src/main.tsx", import.meta.url).pathname],
    target: "bun",
    plugins: [solidPlugin],
    compile: target ? { target, outfile: new URL(`../dist/openaliro-tui-${suffix}`, import.meta.url).pathname } : undefined,
    outdir: target ? undefined : new URL("../dist", import.meta.url).pathname,
    minify: release,
    define: { __OPENALIRO_TUI_REVISION__: JSON.stringify(revision) }
  })
  if (!result.success) {
    console.error(result.logs)
    process.exit(1)
  }
}
