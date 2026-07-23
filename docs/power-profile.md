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
| `WOZ_RSSI_GATE_OPEN_DBM` | -65 | smoothed RSSI at/above this opens the gate |
| `WOZ_RSSI_GATE_CLOSE_DBM` | -75 | at/below this, sustained, closes it |
| `WOZ_RSSI_GATE_CLOSE_HOLD_MS` | 3000 | how long below close before closing |
| `WOZ_RSSI_GATE_SLOPE_DB` | 8 | fast-open rise per 1.5 s window; 0 disables |
| `WOZ_RSSI_GATE_POLL_MS` | 250 | connection RSSI poll period |

Both thresholds are placeholders until the measured curve below pins them.
dBm-to-metres depends on the phone's TX power and the door's RF surroundings;
publish the curve, not the defaults.

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
   `[ALAB]` events: `rssi dbm=`, `gate.hold`, `gate.open`, `gate.close`.
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
