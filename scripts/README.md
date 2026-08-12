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

Prefer a documented Make target when one exists. Run `make help` to see the
supported interface and required variables. Use `make hitl` for `hitl-run.sh`;
pass its optional flags through `HITL_ARGS`.
