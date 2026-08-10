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
| Aliro `SIDE peer=` on CoC open | Implemented (`aliro_ble_zephyr.c`) |
| Witness `ADDR` AdvA filter | Implemented + DFU zips in `build/witness-dfu/` |
| Pi `aliro_bridge.py` | Implemented — CDK serial → ADDR on dongles |


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

1. ~~Flash ADDR-capable witnesses + CDK with `SIDE peer=` emit~~ **done**
2. ~~`side_hitl.py` ADDR path~~ **PASS**
3. ~~Outside/inside static pose capture~~ **PASS** (`omi` +8.4 / −17.9); gate replay PASS
4. ~~`SIDE=1` + SF1 RTT feed~~ **PASS on hardware** — `side_hitl.py` holds the
   probe with `JLinkExe` and reads/writes the RTT telnet server (19021), so one
   connection carries `SIDE peer=` out and SF1 in. Injected SF1 drives the real
   `woz_side_filter_feed`: outside-favouring → `side=2 conf=100` after 3 windows,
   inside → not OUTSIDE, dead band → `side=3`, `ni/no<3` → `side=0 conf=0`.
5. ~~Live walk with the three witnesses feeding SF1~~ **PASS at the door.**
   Outside: `side feed: side=2 conf=100`, `omi` +6..+34 (29 windows). Inside:
   `side=1 conf=100`, `omi` −6..−33 (15 windows). No misclassification. Two
   deployment traps found: a dongle's role is firmware not position (fixed with
   `--role-map`), and the first approach cannot commit in time because the
   witnesses only learn the phone's rotating address at CoC open — the second
   approach inside `--addr-hold-s` grants.
6. **WORKING 2026-08-11.** Passive UWB approach unlock fires through the
   fail-closed side gate on real hardware, with a clean `Secured/relock`
   afterwards. Five firmware bugs had to be fixed to get there, none of them
   in the classifier and none found by walking:
   - `aliro_approach.c:547/597` cleared `ap->locked` BEFORE returning the
     unlock action, so a gate refusal desynchronised the controller and the
     unlock was never re-offered. Added `aliro_approach_veto()`.
   - `main.c` held `side_dec` as a snapshot the filter could only age while
     being fed: a dead feed froze a committed OUTSIDE open forever
     (fail-OPEN, observed live). Added `SIDE_FEED_WATCHDOG_MS` (5000).
   - `s_secured_undelivered` was RAM-only, so a reboot destroyed the one
     thing that could correct a Wallet stranded showing the door open. Now
     true at boot.
   - `confidence_min = 70` against `60 + (|oi| - margin) * 4` silently turned
     a declared 6 dB margin into a real 9 dB one. Now 60, so
     `rssi_outside_margin_db` is the single knob.
   - `woz_side.c` reset `cand_n` on ANY UNKNOWN, including the UNKNOWN that
     just means "too few packets this window". 35% of windows fell below
     `min_pkts_per_anchor`, so three CONSECUTIVE agreeing windows was near
     unreachable. Data gaps now hold the candidate; faults still reset it.
   Also `evidence_fresh_ms` 1500 -> 4500, which must exceed the ~2 s SF1
   cadence or every non-committing window reads EVIDENCE_STALE.
7. Geometry: inside witness moved ~3 m in, heights matched, threshold dongle
   dropped (quorum is INSIDE|OUTSIDE only). `omi` mean went ~0 -> +6.2 dB.
   Still marginal: a later run measured +3.6 mean, 31% of windows over +6.
   More separation is the remaining physical lever.
8. **Bisected 2026-08-10:** with `side_hitl.py --force-sf1 outside` holding the
   gate open (`side=2 conf=100 flags=0x00`), the Wallet animates and the lock
   unlocks on approach. So ranging, the approach controller and the grant path
   are healthy on the SIDE build — no regression. The only blocker is witness
   evidence: geometry, and the ~6 s (`agree_windows` x `WITNESS_WINDOW_MS`)
   needed before a side commits.
7. `SIDE peer=` is emitted once, at CoC open. A tool attached mid-session never
   arms ADDR and the lock sits on boot-state `flags=0x80` QUORUM_FAIL. Start
   the capture before the phone connects, or re-emit the peer periodically.
8. Production witness→lock transport (UART/Matter) without stealing J-Link
7. Investigate short Aliro session (UWB IDLE → DEINIT) if unlock does not range

## Key paths

- Policy / classifier: `modules/woz_anchor/include/woz_side.h`, `src/woz_side.c`
- Binary log: `modules/woz_anchor/include/woz_side_log.h`, `src/woz_side_log.c`
- Lock gate: `apps/dwm3001cdk-lock/src/main.c` (`CONFIG_WOZ_SIDE_GATE`)
- Overlay: `apps/dwm3001cdk-lock/overlay-side.conf` (`make build SIDE=1`)
- Witness app: `examples/zephyr/ble-witness/`
- Collector: `tools/side-capture/`
- Secondary UWB scaffold: `examples/zephyr/secondary-uwb/`
