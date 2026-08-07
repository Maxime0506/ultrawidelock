# anchor — anchor-to-anchor DS-TWR bench link

Stage A of `internal/two-anchor-plan.md`. Two boards range **each other**, with no
phone, no credential and no Aliro session anywhere in the image.

```bash
make anchor-pair                 # builds both halves of today's pair
make anchor-flash ROLE=responder ANCHOR_BOARD=decawave_dwm3001cdk CDK_PROBE=<VID:PID:Serial>
make anchor-monitor ROLE=responder ANCHOR_BOARD=decawave_dwm3001cdk CDK_PROBE=<VID:PID:Serial>
```

## Two ways to get a dead link with no error message

Both of these present identically — the initiator counts rounds, the responder
counts nothing, and neither logs a fault — so check them before suspecting the
radio.

**1. Both boards must be built with the SAME `ANCHOR_REPLY_DELAY_US`.** The
responder schedules its reply at that offset from the POLL it heard, and the
initiator sizes its receive window from *its own* copy of the value. Build one
side at 3000 and the other at 6000 and the initiator's window closes before the
reply arrives, every round, for ever. `make anchor-pair` is safe because both
halves take the default; it only bites when you override one side to chase a
`late` counter and forget the other. Change it on both, or not at all.

**2. Pin `CDK_PROBE` on every flash and monitor.** Two probes on one machine
enumerate in an order that is not stable between sessions — `mk/cdk.mk` records
the same two J-Links coming back reversed twenty minutes apart with no cable
touched — and a flash to the wrong index is silent. Get the serials from
`probe-rs list` and use them every time:

```bash
probe-rs list
make anchor-flash ROLE=responder ANCHOR_BOARD=decawave_dwm3001cdk CDK_PROBE=<serial-A>
make anchor-flash ROLE=initiator ANCHOR_BOARD=nrf5340dk/nrf5340/cpuapp CDK_PROBE=<serial-B>
```

## Why it is this small

It builds at the `CONFIG_WOZ_UWB` tier and nothing above it. There, `uwb_seam.h`
inlines its four helpers straight to the decadriver and `woz_uwb` compiles only
`uwb_min.c`, so there is no CCC engine, no key schedule and no credential in the
image. The reader tiers exist to serve a phone; both ends of this link are ours.

Two consequences worth knowing:

- The per-frame diagnostic trace **cannot** exist here. `woz_uwb_diag_on` and every
  `DIAGK` site live in `ccc_shim_rx.c`, which this tier does not compile, so the
  rule the lock enforces at runtime is enforced here by the link.
- The whole exchange polls `SYS_STATUS` in thread context instead of taking DW3000
  callbacks, which is why nothing here can log from a ranging callback by accident.

## The round

```
initiator                     responder
  t1  POLL  ------------------>  t2
  t4  <------------------  RESP  t3   (delayed TX, ANCHOR_REPLY_DELAY_US)
  t5  FINAL ------------------>  t6   (delayed TX, ANCHOR_REPLY_DELAY_US)
             carries t_round1 = t4-t1 and t_reply2 = t5-t4
```

The responder holds t2, t3 and t6, so the FINAL's two intervals complete the set and
the responder is what computes the distance. The estimator is `ds_twr_tof_signed()`
from `modules/woz_uwb/src/fira/ds_twr.c`, the same four-term formula the
phone-facing path uses, already covered by the host suite.

STS is **off** on this link. `uwb_min.c`'s baseline PHY runs `DWT_STS_MODE_ND`,
which radiates the scrambled sequence and no payload; the FINAL has to carry two
32-bit intervals, so this app reconfigures to SP0. Nothing is given up by that:
STS defends a distance against someone who wants it shortened, and there is no
untrusted party on an anchor-to-anchor link. The phone-facing link keeps its STS
and is not touched by any of this.

## Calibration

`ANT_DLY=<DTU>` sets `CONFIG_ANCHOR_ANT_DLY_DTU`, subtracted from the time-of-flight
before it becomes a distance. It defaults to 0, which means uncalibrated — and
uncalibrated is what every build in this repository has been, because nothing here
has ever programmed the DW3000's own `TX_ANTD` / `CIA_CONF` antenna-delay registers.

It is a **per-pair** constant, not a per-board one: solving it at a known distance
with two boards absorbs both ends at once. Right for a fixed pair, wrong the moment
a third board joins. Procedure and the expected magnitude (near 16,000 DTU) are in
the plan; `anchor/Kconfig` carries the reasoning.

## Reading the output

Both boards print the same line every 100 rounds, on RTT:

```
ANCHOR rounds=100 ranges=98 to=2 late=0 rej=0 t5err=0 last_mm=1004
```

- `late` non-zero means a delayed TX was refused because its instant had already
  passed. Raise `ANCHOR_REPLY_DELAY_US`; it is the first knob, and the only one
  that should move on this evidence.
- `t5err` is the worst gap between the TX RMARKER the schedule predicted and the one
  the chip reported. The initiator has to predict t5 to put it inside the FINAL, so
  this number is what proves the prediction is a constant the calibration absorbs
  rather than something that drifts.
- `rej` counts distances outside the plausibility band (-30 cm to 30 m), which on
  this link means a timestamp came from the wrong round.

## Soak

`overlays/soak.conf` raises the RTT ring to 8 KB, adds timestamps and turns on the
thread analyzer. The ring is NOINIT and accumulates across resets, so at 4 KB the
**end** of a long run is what goes missing — `NO_BLOCK_SKIP` drops the newest
writes once it is full.
