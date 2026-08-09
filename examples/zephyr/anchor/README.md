# Zephyr anchor example

This application is a two-anchor double-sided two-way-ranging bench. It is
separate from the lock applications and does not change what either board boots
as a lock.

Build one role at a time:

```sh
make anchor-build ROLE=initiator ANCHOR_BOARD=nrf5340dk/nrf5340/cpuapp
make anchor-build ROLE=responder ANCHOR_BOARD=decawave_dwm3001cdk
```

Build the standard pair with:

```sh
make anchor-pair
```

`ROLE` accepts `initiator` or `responder`. `ANT_DLY=<dtu>` supplies the
calibrated lumped antenna delay; omitting it leaves the pair uncalibrated. Use
`make anchor-flash` and `make anchor-monitor` with the same `ROLE` and
`ANCHOR_BOARD` values used for the build.
