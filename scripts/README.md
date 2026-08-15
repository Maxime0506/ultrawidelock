# Scripts

`scripts/` contains command-line helpers for top-level Make targets, device
operations, and release workflows.

| Group | Scripts |
|---|---|
| Setup and environment | `bootstrap.sh`, `toolchain.sh`, `check-signing-key.sh` |
| DWM3001CDK operations | `cdk-dfu.sh`, `cdk-find-probe.sh`, `cdk-rtt-elf-check.sh` |
| Firmware size | `cdk-size.py`, `cdk-size-compare.py`, `cdk-size-baseline.py` |
| Delta update and SMP | `ultrawidelock_patch.py`, `ultrawidelock_push.py`, `ultrawidelock_smp.py` |
| Provisioning | `ultrawidelock-enroll.py`, `spake2p_verifier.py` |
| Release and validation | `release-bundle.sh`, `hitl-run.sh`, `test-runner.sh` |
| Shared shell library | `lib/ui.sh` |

`lib/ui.sh` is sourced, not run: it is the progress display behind the Make
targets that take minutes (`make test`, `make cbmc`, `make check`). On a
terminal it draws a step counter, a percentage, a bar and the elapsed time; in a
pipe, a file or CI it prints one line per step and no escape sequences, and the
wrapped command's own output goes to stdout untouched either way. Set
`ULTRAWIDELOCK_UI=0` to force the plain form, `1` to force the drawn one, and
run `scripts/lib/ui.sh --self-test` to check it against a terminal that is
missing colour, UTF-8, width or `$TERM`.

Prefer a documented Make target when one exists. Run `make help` to see the
supported interface and required variables. Use `make hitl` for `hitl-run.sh`;
pass its optional flags through `HITL_ARGS`.
