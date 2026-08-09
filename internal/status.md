# Inside-vs-outside side gate — status

Updated: 2026-08-10

**Answer:** Fail-closed OUTSIDE-only passive unlock is implemented and host-tested; default CDK image is unchanged. `SIDE=1` adds ~524 B flash / ~128 B RAM and withholds passive unlock until witnesses prove OUTSIDE.

## What exists now

| Piece | Status |
|---|---|
| Baseline CDK size | FLASH 400576 / RAM 115940 (92.37% / 88.46%) |
| `woz_side` classifier + temporal filter | OUTSIDE / INSIDE / THRESHOLD / UNKNOWN |
| `woz_side_may_passive_unlock` | Fail-closed; UNKNOWN never unlocks |
| 48-byte binary decision log | `woz_side_log` |
| CDK wiring | `SIDE=1` → gates PREDICT **and** THRESHOLD |
| nRF52840 witness | Builds (`WITNESS_ROLE=outside` verified) |
| Pi collector | `tools/side-capture/collect.py` |
| Secondary UWB | Scaffold only — **not proven** |
| Matter witness control plane | **Not implemented yet** (UART path first) |

Legacy `woz_fusion_may_predict` (fail-open, OUTSIDE withholds) is untouched when `SIDE` is off.

## Verification

| Command | Result |
|---|---|
| `make build` | PASS |
| `make cdk-size CDK_SIZE_REPORTS=0` | baseline recorded |
| `make build SIDE=1 CDK_BUILD=build/cdk-side` | PASS, `CONFIG_WOZ_SIDE_GATE=y` |
| side size | FLASH **+524 B**, RAM **+128 B** |
| `bash tests/host/run.sh` | **4442 passed** (`woz_side`, `woz_side_replay`) |
| `make witness-build WITNESS_ROLE=outside …` | PASS (~93 KB flash on nRF52840) |

Ops guide: `examples/zephyr/ble-witness/SIDE_GATE.md`

### Size detail

Baseline (`build/cdk-matter`, overlays `overlay-thread.conf;overlay-lto.conf`):

| region | size | used | free | used% |
|--------|------|------|------|-------|
| FLASH | 433664 | 400576 | 33088 | 92.37% |
| RAM | 131072 | 115940 | 15132 | 88.46% |

`SIDE=1` (`build/cdk-side`, adds `overlay-side.conf`):

| region | used | Δ vs baseline |
|--------|------|---------------|
| FLASH | 401100 | **+524 B** |
| RAM | 116068 | **+128 B** |

## Risks

1. **No live witness→lock transport yet** — with `SIDE=1` and no features fed, every passive unlock is withheld (intentional). Need UART/Matter summary ingest next.
2. **Rule baseline only** — differential RSSI + hysteresis; emlearn side model needs real labelled trajectories.
3. **No iPhone HITL in this session** — completion criterion not hardware-proven yet.
4. **Multi-anchor iPhone UWB** — explicitly unsupported until demonstrated.

## Next hardware loop

1. Flash three witnesses (`inside` / `outside` / `threshold`)
2. Capture labelled walks with `tools/side-capture/collect.py`
3. Run `--baseline`, then wire compact summaries into the lock’s `woz_side_filter_feed`
4. Only then enable `SIDE=1` on the door node for approach tests

## Key paths

- Policy / classifier: `modules/woz_anchor/include/woz_side.h`, `src/woz_side.c`
- Binary log: `modules/woz_anchor/include/woz_side_log.h`, `src/woz_side_log.c`
- Lock gate: `apps/dwm3001cdk-lock/src/main.c` (`CONFIG_WOZ_SIDE_GATE`)
- Overlay: `apps/dwm3001cdk-lock/overlay-side.conf` (`make build SIDE=1`)
- Witness app: `examples/zephyr/ble-witness/`
- Collector: `tools/side-capture/`
- Secondary UWB scaffold: `examples/zephyr/secondary-uwb/`
