# Inside vs outside — build, flash, commission, calibrate, place

## What this covers
- Fail-closed side gate (`woz_side`) with OUTSIDE / INSIDE / THRESHOLD / UNKNOWN
- Binary 48-byte decision log (`woz_side_log`)
- nRF52840 BLE witness firmware (inside / outside / threshold)
- Raspberry Pi JSONL collector + differential-RSSI baseline tool
- DWM3001CDK integration behind `SIDE=1` (default build unchanged)
- Secondary UWB experiment scaffold (explicitly unproven)

## Baseline (unchanged Matter image)
```
make dfu-key          # once per checkout
make build
make cdk-size CDK_SIZE_REPORTS=0
```
The committed baseline (`make cdk-size-check`) is the reference for what the
unchanged image weighs.

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

## Feeding the lock

The lock ingests one line per witness window on RTT down-buffer 0:

```
SF1 in=<dbm> out=<dbm> th=<dbm> ni=<n> no=<n> nt=<n>
```

`in`/`out`/`th` are the mean RSSI each witness heard over its window and
`ni`/`no`/`nt` the packet counts. Build the lock with `SIDE=1` so
`CONFIG_WOZ_SIDE_FEED_RTT` and `CONFIG_WOZ_SIDE_PEER_EMIT` are on, then drive
the buffer with any host that can hold an RTT connection.

This is a bench path, not a product one: it needs a debug probe attached for
the life of the session. A witness link over UART or Matter is the work that
replaces it.

## Telling the witnesses which advertiser is the credential

With `CONFIG_WOZ_SIDE_PEER_EMIT=y` the lock logs `SIDE peer=<AdvA> type=…` when
the Aliro L2CAP channel opens and `SIDE peer=clear` when it closes. Forward that
address to each witness as an `ADDR` command and they will summarise one phone
instead of every advertiser in the room.

That address is personal data, which is why the option is default n and why
captured runs are not committed (see `.gitignore`). Do not enable it in a
shipped image.

## Matter control plane
Not implemented yet. Control/summaries will use a vendor cluster;
raw high-rate samples stay on USB/UART to the Pi.

## Unsupported / not yet proven
- Multi-anchor stock-iPhone UWB ranging
- Absolute RSSI as distance
- Fail-open passive unlock on UNKNOWN (explicitly rejected)
- Unlock authority on Pi or witnesses (forbidden)
