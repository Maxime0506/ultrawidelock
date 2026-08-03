<!-- generated documentation — edit the source, not this file -->
# `scripts/`

| subsystem | about |
|---|---|
| [`scripts/bootstrap.sh`](bootstrap.sh.md) | bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream. |
| [`scripts/build-nrf5340dk.sh`](build-nrf5340dk.sh.md) | build-nrf5340dk.sh {build\|rebuild\|flash\|flash-erase\|build-flash} — build the |
| [`scripts/cdk-dfu.sh`](cdk-dfu.sh.md) | cdk-dfu.sh — push a signed image to the DWM3001CDK over MCUboot serial recovery. |
| [`scripts/cdk-rtt-elf-check.sh`](cdk-rtt-elf-check.sh.md) | Refuse to attach RTT with an ELF the board is not running. |
| [`scripts/cdk-size-baseline.py`](cdk-size-baseline.md) | cdk-size-baseline.py — turn a size report into the committed baseline. |
| [`scripts/cdk-size-compare.py`](cdk-size-compare.md) | cdk-size-compare.py — head against the recorded baseline, as a gate. |
| [`scripts/cdk-size-notify.py`](cdk-size-notify.md) | cdk-size-notify.py — say what a change cost the CDK image, in Discord. |
| [`scripts/cdk-size.py`](cdk-size.md) | cdk-size.py — what the DWM3001CDK image costs, as a machine-readable record. |
| [`scripts/check-approtect.sh`](check-approtect.sh.md) | check-approtect.sh — refuse to ship an image that locks APPROTECT. |
| [`scripts/check-signing-key.sh`](check-signing-key.sh.md) | check-signing-key.sh — refuse to build a bootloader that anybody can sign for. |
| [`scripts/check-uwb-seam.sh`](check-uwb-seam.sh.md) | check-uwb-seam.sh — keep the CCC STS seam impossible to bypass. |
| [`scripts/deadcode-codechecker.sh`](deadcode-codechecker.sh.md) | deadcode-codechecker.sh — CodeChecker over the real firmware build. |
| [`scripts/deadcode-graph.sh`](deadcode-graph.sh.md) | deadcode-graph.sh — find functions nothing calls, using the documate code graph. |
| [`scripts/deadcode-size.sh`](deadcode-size.sh.md) | deadcode-size.sh — flash cost of the functions nothing calls. |
| [`scripts/deadcode-tidy.sh`](deadcode-tidy.sh.md) | deadcode-tidy.sh — run clang-tidy against the REAL firmware build. |
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
| [`scripts/woz_patch.py`](woz_patch.md) | Build a signed delta patch for the DWM3001CDK's over-the-air update path. |
| [`scripts/woz_push.py`](woz_push.md) | Push a signed delta patch to a DWM3001CDK over Bluetooth. |
| [`scripts/woz_smp.py`](woz_smp.md) | Push a delta patch to the board over SMP, the way a phone would. |
| [`scripts/ws-seed.sh`](ws-seed.sh.md) | ws-seed.sh — give this git worktree its own NCS workspace, cheaply. |
