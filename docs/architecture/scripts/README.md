<!-- generated documentation — edit the source, not this file -->
# `scripts/`

| subsystem | about |
|---|---|
| [`scripts/bootstrap.sh`](bootstrap.sh.md) | bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream. |
| [`scripts/build.sh`](build.sh.md) | build.sh {build\|rebuild\|flash\|flash-erase\|build-flash} — build the Aliro |
| [`scripts/docs-publish.sh`](docs-publish.sh.md) | docs-publish.sh — snapshot the rendered site/ onto the local gh-pages branch. |
| [`scripts/docs.sh`](docs.sh.md) | docs.sh — build the documentation site into site/. |
| [`scripts/flash_html.py`](flash_html.md) | Render a release FLASH.md into a self-contained FLASH.html. |
| [`scripts/test-runner.sh`](test-runner.sh.md) | Pretty umbrella runner for every host-side suite: one banner, live per-check |
| [`scripts/toolchain.sh`](toolchain.sh.md) | toolchain.sh — what the CI gates need, whether this host has it, how to get it. |
| [`scripts/twin-suite.sh`](twin-suite.sh.md) | The web-twin suite for the umbrella runner (make check): the constant-drift |
| [`scripts/twin-wasm.sh`](twin-wasm.sh.md) | Build the web twin's firmware: modules/woz_uwb + the tests/host shim compiled |
| [`scripts/verify.sh`](verify.sh.md) | Pre-push sweep: every CI gate that a host can run, in one shot. |
| [`scripts/ws-seed.sh`](ws-seed.sh.md) | ws-seed.sh — give this git worktree its own NCS workspace, cheaply. |
