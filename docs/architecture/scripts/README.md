<!-- generated documentation — edit the source, not this file -->
# `scripts/`

| subsystem | about |
|---|---|
| [`scripts/bootstrap.sh`](bootstrap.sh.md) | bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream. |
| [`scripts/build-nrf5340dk.sh`](build-nrf5340dk.sh.md) | build-nrf5340dk.sh {build\|rebuild\|flash\|flash-erase\|build-flash} — build the |
| [`scripts/check-approtect.sh`](check-approtect.sh.md) | check-approtect.sh — refuse to ship an image that locks APPROTECT. |
| [`scripts/check-uwb-seam.sh`](check-uwb-seam.sh.md) | check-uwb-seam.sh — keep the CCC STS seam impossible to bypass. |
| [`scripts/docs-publish.sh`](docs-publish.sh.md) | docs-publish.sh — snapshot the rendered site/ onto the local gh-pages branch. |
| [`scripts/docs.sh`](docs.sh.md) | docs.sh — build the documentation site into site/. |
| [`scripts/flash_html.py`](flash_html.md) | Render a release FLASH.md into a self-contained FLASH.html. |
| [`scripts/presence_runtime.py`](presence_runtime.md) | Build the minimal, deterministic presence runtime transfer archive. |
| [`scripts/security-attest.sh`](security-attest.sh.md) | security-attest.sh — can somebody who downloaded a release prove where it came from? |
| [`scripts/security-ct.sh`](security-ct.sh.md) | security-ct.sh — secret-dependent branches and table lookups in the CCC key ladder. |
| [`scripts/security-diff.sh`](security-diff.sh.md) | security-diff.sh — the structural half of the malicious-change gate. |
| [`scripts/security-fw.sh`](security-fw.sh.md) | security-fw.sh — the shipped artifact, which every other gate in this repo reasons about only |
| [`scripts/security-web.sh`](security-web.sh.md) | security-web.sh — the browser half of the supply chain, which nothing else in this repo looks at. |
| [`scripts/security-workspace.sh`](security-workspace.sh.md) | *first commit: "security: add the eight-gate scanning lane"* |
| [`scripts/security.sh`](security.sh.md) | security.sh — the four fast security gates, in one place. |
| [`scripts/spake2p_verifier.py`](spake2p_verifier.md) | Derive a SPAKE2+ verifier (w0 and L) for a Matter setup passcode. |
| [`scripts/test-runner.sh`](test-runner.sh.md) | Pretty umbrella runner for every host-side suite: one banner, live per-check |
| [`scripts/toolchain.sh`](toolchain.sh.md) | toolchain.sh — what the CI gates need, whether this host has it, how to get it. |
| [`scripts/twin-suite.sh`](twin-suite.sh.md) | The web-twin suite for the umbrella runner (make check): the constant-drift |
| [`scripts/twin-wasm.sh`](twin-wasm.sh.md) | Build the web twin's firmware: modules/woz_uwb + the tests/host shim compiled |
| [`scripts/verify.sh`](verify.sh.md) | Pre-push sweep: every CI gate that a host can run, in one shot. |
| [`scripts/ws-seed.sh`](ws-seed.sh.md) | ws-seed.sh — give this git worktree its own NCS workspace, cheaply. |
