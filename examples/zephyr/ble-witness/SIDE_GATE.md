# Inside vs outside — build, flash, commission, calibrate, place

## What shipped in this change
- Fail-closed side gate (`woz_side`) with OUTSIDE / INSIDE / THRESHOLD / UNKNOWN
- Binary 48-byte decision log (`woz_side_log`)
- nRF52840 BLE witness firmware (inside / outside / threshold)
- Raspberry Pi JSONL collector + differential-RSSI baseline tool
- DWM3001CDK integration behind `SIDE=1` (default build unchanged)
- Secondary UWB experiment scaffold (explicitly unproven)

## Baseline (unchanged Matter image)
```
make dfu-key          # once per worktree
make build
make cdk-size CDK_SIZE_REPORTS=0
```
Observed on this worktree:
| region | size | used | free | used% |
|--------|------|------|------|-------|
| FLASH | 433664 | 400576 | 33088 | 92.37% |
| RAM | 131072 | 115940 | 15132 | 88.46% |

## SIDE=1 image (gate linked in)
```
make build SIDE=1 CDK_BUILD=build/cdk-side
make cdk-size CDK_SIZE_REPORTS=0 CDK_BUILD=build/cdk-side
```
| region | used | Δ vs baseline |
|--------|------|---------------|
| FLASH | 401100 | **+524 B** |
| RAM | 116068 | **+128 B** |

`CONFIG_WOZ_SIDE_GATE=y` verified in the side image `.config`.

## Host tests
```
make check
```
New suite: `woz_side` (policy fail-closed, spike hysteresis, log CRC).

## Enable the side gate on the lock
```
make build SIDE=1 CDK_BUILD=build/cdk-side
make cdk-size CDK_SIZE_REPORTS=0 CDK_BUILD=build/cdk-side
```
With `SIDE=1` and no live witness evidence, **all passive unlocks are withheld**.
NFC Express Mode, Apple Home, and mechanical operation are not gated.

## BLE witnesses
```
make witness-build WITNESS_ROLE=outside WITNESS_BOARD=nrf52840dk/nrf52840
make witness-build WITNESS_ROLE=inside  WITNESS_BOARD=nrf52840dk/nrf52840
make witness-build WITNESS_ROLE=threshold WITNESS_BOARD=nrf52840dk/nrf52840
```
Placement:
1. Outside: approach face, ~0.3–1 m from door plane
2. Inside: protected face, mirrored
3. Threshold: lintel / frame, in the plane

## Pi capture
```
python3 tools/side-capture/collect.py --label outside_approaching --uart /dev/ttyUSB0
python3 tools/side-capture/collect.py --replay captures/*.jsonl --baseline
```

## Secondary UWB (experimental)
```
make secondary-uwb-build
```
Not required for the side gate. Do not treat multi-anchor iPhone UWB as available
until demonstrated.

## Matter control plane
Not implemented in this slice. Control/summaries will use a vendor cluster;
raw high-rate samples stay on USB/UART to the Pi.

## Unsupported / not yet proven
- Multi-anchor stock-iPhone UWB ranging
- Absolute RSSI as distance
- Fail-open passive unlock on UNKNOWN (explicitly rejected)
- Unlock authority on Pi or witnesses (forbidden)
