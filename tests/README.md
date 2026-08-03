# Tests

Everything here runs on a laptop: plain C compiler, no NCS toolchain, no ESP-IDF, no
hardware. Hardware truth comes separately from the manual
[hardware validation checklist](../docs/hardware-validation.md).

| Command | What it runs | CI workflow |
|---|---|---|
| `make test` | [`host/`](host/): protocol KATs, state machines, radio/backend fakes, NFC transports, and diagnostic/tooling suites, compiled against the `WOZ_PORT_HOST` backend with the Zephyr shims in `host/shim/` | host-tests |
| `make coverage` | Same suite instrumented (clang), HTML report, enforced line-coverage floor | host-tests |
| `make test-san` | Same suite under ASan + UBSan | sanitizers |
| `make fuzz` | libFuzzer (CI) or corpus replay (macOS) on the wire-facing parsers | fuzz |
| `make cbmc` | CBMC bounded proofs of memory safety for the wire parsers | cbmc |
| `make verify` | Every host-runnable CI gate: a short serial tripwire followed by parallel lanes; missing required tools fail the sweep, and CBMC is opt-in with `WITH_CBMC=1` | Not applicable |
| `make test-port` | [`../ports/esp32/test/`](../ports/esp32/test): the ESP32 port suite (port headers, crypto KATs, codec, provisioning) | port-tests |
| `make test-ws` | [`tooling/ws_seed_test.sh`](tooling/ws_seed_test.sh): hermetic tests of per-worktree workspace seeding | tooling |
| `make test-verify` | [`tooling/verify_test.sh`](tooling/verify_test.sh): tests for the sweep above: that its gate table still covers every CI job, and that a missing tool or an unmet coverage floor fails it instead of passing quietly | tooling |
| `tooling/patch_drift_check.sh` | Verifies every nRF patch still applies to the pinned upstream revisions (sparse network fetch, no workspace) | patch-drift |

CI additionally runs shellcheck over every script (tooling), clang-format and
clang-tidy over `modules/`, a linter over the workflows themselves (workflow-lint),
and compile-gates all three firmware targets, DWM3001CDK, nRF5340 DK and ESP32
(firmware-builds). Hardware behavior stays gated on the bench, not the runner: see
[hardware-validation.md](../docs/hardware-validation.md).
