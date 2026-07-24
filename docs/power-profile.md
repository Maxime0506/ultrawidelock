# RSSI power gate and the power-profile study

Continuous UWB is what keeps an Aliro reader off battery power: the DW3000
listens with its receiver hard-on, and until now it started doing that the
moment a phone finished auth, which happens at BLE range (tens of metres).
The RSSI power gate (`CONFIG_WOZ_RSSI_GATE`, default on) holds the one message
the phone waits for before initiating ranging, Reader-Status-AP-Completed
(Aliro 1.0, 11.7.3.4.1), until the smoothed BLE connection RSSI says the phone
is in the last few metres. Until then the UWB radio stays powered off; a
sustained fade below the close threshold tears ranging down again and drops
the link, and the phone simply re-runs the fast auth on its next approach.

The gate itself is `modules/woz_aliro/src/aliro_rssi_gate.c`: EWMA smoothing,
open/close hysteresis with a close hold, and an optional rise-rate fast open
so fast walkers are not penalized by the smoothing lag. It is pure logic with
a host test suite; the ESP32 transport polls the controller RSSI once per
`CONFIG_WOZ_RSSI_GATE_POLL_MS` while the Aliro L2CAP channel is up.

## Knobs

| Kconfig | default | meaning |
|---|---|---|
| `WOZ_RSSI_GATE` | y | the gate; off = radio arms right after auth, as before |
| `WOZ_RSSI_GATE_OPEN_DBM` | -55 | smoothed RSSI at/above this opens the gate |
| `WOZ_RSSI_GATE_CLOSE_DBM` | -65 | at/below this, sustained, closes it |
| `WOZ_RSSI_GATE_CLOSE_HOLD_MS` | 3000 | how long below close before closing |
| `WOZ_RSSI_GATE_SLOPE_DB` | 8 | fast-open rise per 1.5 s window; 0 disables |
| `WOZ_RSSI_GATE_MAX_HOLD_MS` | 0 | cap on the AP-Completed hold; 0 = unbounded (see below) |
| `WOZ_RSSI_GATE_POLL_MS` | 250 | connection RSSI poll period |

Changing a threshold means changing `sdkconfig.defaults`, not just the Kconfig
`default`. A Kconfig default only seeds a **fresh** sdkconfig; an app that
already has one keeps its old value forever, so editing the default alone
silently ships the previous thresholds and every bench number after it is
measuring the old build. Check what actually shipped:

```
grep WOZ_RSSI_GATE ports/esp32/apps/matter-lock/sdkconfig
```

## How long the phone will wait

The hold is not free and the phone sets the budget. Measured on an iPhone
against this reader, three consecutive holds ended at **1.873, 1.899 and
1.899 s**, each time with the phone terminating the link (`GAP disconnect
reason=531`, i.e. `0x200 + 0x13 BLE_ERR_REM_USER_CONN_TERM`) and immediately
reconnecting to try again. Our own gate-close disconnect reads 534
(`0x16 BLE_ERR_CONN_TERM_LOCAL`); the pair is how you tell who dropped the link.

So a hold does not keep a loitering phone connected and dark. It hands the
teardown to the phone, and a peer parked in the threshold band becomes a
connect/hold/drop cycle. `WOZ_RSSI_GATE_MAX_HOLD_MS` exists to bound that: at
the cap the reader completes the AP anyway and the gate opens as if the level
had qualified, so the ordinary close path still powers the radio down when the
peer leaves. The trace distinguishes the two openings, `gate.open` against
`gate.holdcap`, because they cost very different amounts of radio.

**It defaults to 0, off.** Both arms were run on the bench and the cap lost.
Capped at 1200 ms, the gate released at -75 and -72 dBm with the phone still
roughly 4 m out; the DW3000 then listened to nothing for **2.8 s** in one
session and **6.6 s** in the next before the first trusted range. That is the
waste the gate exists to prevent, and what it buys is the avoidance of a
reconnect that costs one fast re-auth: BLE only, about 200 ms. Seconds of UWB
receiver against a fraction of a second of BLE is not a close call.

The right behaviour for a phone that connects at BLE range and then stays there
is to hold, let it give up, and let it come back when it is closer. The unlock
is not at risk: an unprimed gate still fails open, and the phone demonstrably
retries.

If you set a cap anyway, note it is only checked when an RSSI sample arrives, so
the hold ends up to one `WOZ_RSSI_GATE_POLL_MS` late. Measured: a 1500 ms cap
released at **1695 ms**, leaving 121 ms against a phone that gives up at 1873.
Budget cap + poll, not cap.

dBm-to-metres depends on the phone's TX power and the door's RF surroundings,
so the curve is the deliverable, not the defaults. Here is ours.

## The curve

274 paired samples, one iPhone, one room, from three slow walk-ins with 3 s
pauses at 2.0, 1.5, 1.0 and 0.5 m. Each `range cm=` in the lab trace paired
with the `rssi dbm=` nearest it in time; reproduce with
`power_profile.py capture.log --calibrate`.

| distance | n | median | p10 | p90 | min | max |
|---|---|---|---|---|---|---|
| 0-50 cm | 39 | -47 | -54 | -44 | -58 | -40 |
| 50-100 cm | 41 | -44 | -52 | -40 | -70 | -38 |
| 100-150 cm | 38 | -52 | -56 | -47 | -61 | -41 |
| 150-200 cm | 54 | -53 | -57 | -51 | -61 | -46 |
| 200-250 cm | 54 | -55 | -63 | -51 | -71 | -50 |
| 250-300 cm | 30 | -61 | -70 | -56 | -72 | -51 |
| 300+ cm | 18 | -65 | -70 | -57 | -70 | -56 |

Three things in that table are worth more than the numbers.

**It is flat.** About 18 dB across the whole 0 to 3 m range, and the bins
overlap heavily: the p90 of 250-300 cm (-56) sits above the median of 100-150 cm
(-52). BLE RSSI is a coarse proximity hint here, not a range estimate.

**It is not monotonic.** 50-100 cm reads *stronger* (-44) than 0-50 cm (-47).
At the door the phone is held differently and the body is in a different place;
whatever the mechanism, "closer is stronger" stops being true in the last metre.
Anything that tries to invert this curve into a distance will be wrong there.

**Tails are long.** The 50-100 cm bin has a -70 minimum, 26 dB below its own
median. A single sample proves nothing about distance, which is why the gate
smooths (EWMA) and latches (hysteresis) rather than thresholding raw values.

## Why -55 and not the best-separating threshold

`--calibrate` scores thresholds by how cleanly they split near from far, and on
this data it likes -50: it opens for 74% of samples under 1 m and only 11% of
those beyond it. That is the right answer to the wrong question.

Ranging is not instant. From AP-Completed to the first trusted range is about
1.5 s (`apc` to `trusted` in the lab trace). A gate that opens under 1 m hands
the radio a walking phone that is already at the door, and the bolt waits on
warm-up. The gate has to open with enough lead that ranging is producing
distances by the time it matters, which at normal walking pace means around 2 m.

-55 sits at the 200-250 cm median, so it opens in the 2.0-2.5 m band. It scores
worse on separation (+37 against +63) and that is the trade being made: some
radio-on time beyond the door in exchange for never being the reason an unlock
is late. What keeps an aggressive threshold safe is not the hold cap (off by
default) but the fail-open on an unprimed gate, plus the phone's own willingness
to drop the hold and reconnect on approach.

The old -65/-75 pair came from guesswork and this data retires it: -65 opened
for 93% of samples past 1 m (the gate barely gated, releasing the radio at
2.7 m), and -75 was below the level seen at any distance the rig could still
hold a link at, so the close path effectively never fired.

## What nobody publishes: the numbers

The study this instrumentation exists for: **mA versus unlock latency versus
approach speed** on a bare DW3000, gated versus ungated. Prerequisite reading
for anyone putting this in a retrofit deadbolt.

### Bench setup

1. Power analyzer (PPK2 in source-meter mode, or a Joulescope) inline on the
   DW3000 EVB supply rail. Whole-board works too but buries the UWB delta
   under the ESP32's own draw; the rail is the number that transfers to other
   designs.
2. Serial capture of the reader console with the lab trace on (`lab on`;
   firmware built with `CONFIG_WOZ_ALIRO_LAB=y`, the default). The gate emits
   `[ALAB]` events: `rssi dbm=`, `gate.hold`, `gate.open`, `gate.holdcap`,
   `gate.close`.
3. One power capture per walk-up run, started before the approach begins. The
   parser aligns the capture to the device clock on the DW3000 wake step at
   ranging start (m4); `--shift` overrides if the auto-align picks wrong.

### Scenarios

Run each N=10, one serial log + one power capture per run:

| scenario | gate | what it measures |
|---|---|---|
| approach slow/normal/fast | on | latency cost at the door, per speed |
| approach slow/normal/fast | off (`WOZ_RSSI_GATE=n`) | the ungated baseline |
| approach normal, slope off (`SLOPE_DB=0`) | on | what the fast-open buys |
| approach normal, cap on (`MAX_HOLD_MS=1200`) | on | the capped arm: UWB armed early against reconnects avoided |
| parked outside BLE range | either | true idle floor |
| parked connected, far (hallway) | on | the held state: BLE up, UWB dark |
| walk in and stay (resident) | on | the open question below |

### Reduce

```
python3 tools/power_profile.py capture.log --ppk trace.csv --tag fast --csv study.csv
```

Per walk-up: held time (connect to gate-open), gate-open to bolt, connect to
bolt, UWB-on window, UWB duty cycle, RSSI at open, and mean mA over the idle /
held / UWB-on spans. `--csv` appends so the scenario matrix accumulates into
one file; plot mA against latency per tag and the curve is the deliverable.

## Known limitation (bench question)

If the phone stays parked inside after unlocking, its side eventually stops
ranging (the reader's peer-gone relock fires) while the reader's receiver
keeps listening for the next round until the phone drops BLE. Whether iOS
drops the link promptly is exactly what the resident scenario measures; if it
holds the link for long, the follow-up is a reader-side idle teardown after
peer-gone. The gate's close path already covers the walk-away case.
