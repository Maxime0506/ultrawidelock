# Hardware validation

Automated CI gates the host-side logic (KAT suite, coverage floor, sanitizers, fuzz,
CBMC, the ESP32 port suite) and compile-gates both targets' firmware. What it cannot
exercise is the product itself, which runs against a live iPhone. This checklist is
the manual gate: run
every applicable item before cutting a release, and record the results table in the
release notes (see [`RELEASING.md`](RELEASING.md)).

Two hardware paths have recorded bench evidence: the nRF5340 DK using the legacy
Nordic binary with its default ST25R300/RFAL reader, and ESP32-S3. The in-tree
Aliro stack is now the nRF default, but it does not inherit the legacy binary's
result. It must pass the nRF checklist before release. A release covering only one
target runs that target's rows and records the other as `n/a`.

ESP32-C5 is built and bundled by the release workflow, but has no hardware
validation record. Mark it build-only in release notes until a C5 checklist is
defined and passed. The PN532 variant likewise has automated evidence only and
does not inherit the ST25R300 checklist result.

## nRF5340 DK

### Test setup

- nRF5340 DK with DWM3000EVB (Arduino header) and X-NUCLEO-NFC12A1, wired per
  [`ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay).
- An iPhone (or Apple Watch) with the lock's Aliro key provisioned in Wallet.
- Serial console attached (`make nrf-term`) to observe logs.

### Checklist

| ID | Procedure | Pass criterion |
|---|---|---|
| HV-1 | `make test` on the release commit | Exit 0, all host KATs pass |
| HV-2 | `make rebuild` (pristine) | Exit 0, image links and fits flash |
| HV-3 | Flash a `make selftest` build, boot with no phone present | Boot self-test reports pass on the console |
| HV-4 | Flash the release image (`make flash-erase` for a first flash), boot | Clean boot, `Aliro source stack enabled` appears with no errors, BLE advertising starts |
| HV-5 | Tap the phone on the NFC reader (Express Mode, screen off) | Lock actuates to unlocked; console logs the granted access |
| HV-6 | Relock, then approach from well outside ranging distance, phone pocketed | Lock unlocks on approach with no phone interaction |
| HV-7 | Walk away from the lock | Lock relocks after passing the hysteresis margin, and does not oscillate at the boundary |
| HV-8 | Power-cycle the DK, wait for boot, repeat HV-5 and HV-6 | Both unlock paths work without re-provisioning the key |

## ESP32-S3

No NFC tap path exists on this target, so there is no equivalent of HV-5.

### Test setup

- ESP32-S3 dev board with a DWM3000EVB wired per
  [`docs/esp32-bringup.md`](esp32-bringup.md), including the EVB's
  power-select jumper.
- An iPhone with a key provisioned in Wallet for *this* reader identity. A key minted
  against a different reader will not authenticate.
- Serial console attached (`make monitor` from `ports/esp32/apps/matter-lock`).

### Checklist

| ID | Procedure | Pass criterion |
|---|---|---|
| EV-1 | `make test-port` on the release commit | Exit 0, all host suites pass |
| EV-2 | `make rebuild` in `ports/esp32/apps/matter-lock` | Exit 0; `verify_port.sh` reports the link seam intact and the app fits its partition |
| EV-3 | `make flash-erase`, then boot | Clean boot, onboarding codes printed, no watchdog resets |
| EV-4 | Commission into a home with the printed code | Commissioning completes; `status` shows the fabric |
| EV-5 | Confirm a key lands in the phone's wallet | Key appears, tied to this reader |
| EV-6 | `aliro prov` on the console | Reports a provisioned identity, not the dev-identity fallback warning |
| EV-7 | Approach from well outside ranging distance, phone pocketed | Wallet unlock animation plays and the bolt opens, with no phone interaction |
| EV-8 | Watch the console through EV-7 | Continuous positive distances tracking the approach; no watchdog reset |
| EV-9 | Walk away | Bolt relocks past the hysteresis margin and does not oscillate at the boundary |
| EV-10 | Re-approach within the same session | Unlocks again without a reconnect |
| EV-11 | Power-cycle the board, wait for boot, repeat EV-7 | Unlock works without re-commissioning or re-provisioning |
| EV-12 | `lab on`, then approach from beyond BLE range and watch the trace | `gate.hold` appears and no `rrx`/`rtx` follows until `gate.open`; the radio stays dark while the phone is far |
| EV-13 | Loiter out of range for ~10 s during EV-12 | Repeated `session.start` / `gate.hold` / `session.end` cycles are correct, not a fault; the phone gives up at ~1.9 s and retries |
| EV-14 | Unlock, then stand still at the door for 10 s | No `relock.sent`, bolt does not cycle. iOS pauses ranging when still; a relock here is the regression |
| EV-15 | Unlock, then leave briskly | `relock.sent` appears **before** `session.end`, and the phone shows locked as you go |
| EV-16 | Re-approach after EV-15 | No `relock.sent` between `ph.apc` and the grant, i.e. the Wallet does not flash locked then unlocked |
| EV-17 | Score any capture that reached UWB-active with `tools/aliro_lab.py` | The `order` check passes. `ph.m1` before `ph.m2` and `ph.m3` before `ph.m4rx`: setup stamps follow message identity, not arrival order |
| EV-18 | Read the `ranging setup:` line of that report | Reads `rrx SUPPL id=0, rrx IRS, rtx M1, rrx M2, rtx M3, rrx M4`. A bare `rrx id=` with no protocol means pre-fix firmware |

EV-7 is the row that matters most and the one most easily faked: the bolt moving is not
a pass. The Wallet animation is the pass criterion, because that is what proves the
reader told the phone it granted access rather than just actuating locally.

EV-12 to EV-16 gate the RSSI power gate and the relock policy; see
[`power-profile.md`](power-profile.md) for what each measurement means and for the
thresholds they depend on. EV-14 and EV-16 are regression rows: both behaviours were
shipped broken once and are invisible unless specifically looked for.

EV-17 and EV-18 are the third such row. The ranging-setup latency stamps used to be
assigned by arrival order, and the phone sends a proto-3 (supplementary-service) SDU
ahead of Initiate-Ranging-Session, so every device-to-reader label sat one frame early
— the report claimed M2 arrived before M1 was sent. Nothing in the protocol depended
on it, but every setup timing read from those captures was wrong. Measured on the
fixed firmware, the setup exchange is IRS +2.0 ms M1, +27.8 ms M2, +2.4 ms M3,
+27.7 ms M4; the old labelling reported that as a 29.7 ms IRS-to-M4 span, which was
really IRS to M2.

## Recording results

Copy the relevant tables into the release notes with a Result column (`pass` / `fail` /
`n/a`), plus: firmware commit hash, toolchain version (NCS, or ESP-IDF and esp-matter),
board revision, phone model, and iOS version. A release ships only when every applicable
row is `pass`.
