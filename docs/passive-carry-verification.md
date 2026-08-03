# Passive carry verification

Hands-free unlock should arm only while the credential is actually being carried by a
walking person, not while the phone sits on a hall table, rides in a relay, or was lifted
off a stolen owner. The signal that tells these apart is already on the wire: the tiny
distance residuals that ride on top of a UWB approach track are a gait signature. A phone
carried by a walking body breathes a few centimetres per step at the walking cadence; a
stationary phone does not.

This page describes the design and the offline instrument that scores it. The firmware
gate is deliberately not shipped yet: it is gated on a bench experiment (E1 below) run
against a real iPhone, because whether cm-quantized 5.2 Hz ranges can separate household
members is a question only real data answers.

**Status.** Offline analysis tool (`tools/aliro_gait.py`) built and validated on a real
ESP32-S3 + DWM3000EVB capture. Firmware Tier 1 (motion gate) and Tier 2 (carrier
identity) are specified but unbuilt, pending the E1 result. The capture path itself needs
no new firmware: it reuses the Aliro Lab range trace.

## Why it closes a real gap

Today the reader's hands-free decision is a pure proximity threshold: a conditioned median
range under an unlock band, with relock and dwell counters
(`ports/esp32/apps/matter-lock/main/app_main.cpp`). There is no motion gate. An
authenticated phone that is simply near the door unlocks it, whether or not anyone is
holding it. That is what makes the hall-table phone, the BLE relay to a phone upstairs, and
the stolen-but-stationary phone all succeed. A motion gate closes all three, because a
stationary phone has no gait residual to show.

## The signal, honestly

- The reader logs one trusted range per ranging block. On the observed iPhone the block is
  192 ms, so the sample rate is about 5.2 Hz and the usable band tops out near 2.6 Hz
  (Nyquist). Walking cadence, roughly 1.2 to 2.2 Hz, fits under that. Waveform detail above
  it does not: cadence is recoverable, fine stride shape is not.
- The rate is the phone's to set, not the reader's. The RAN multiplier the initiator sends
  during ranging setup is its maximum ranging frequency; the reader already selects the
  fastest the phone permits (`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`). There is no
  mid-session message to raise it. 5.2 Hz is the ceiling on this hardware.
- The gait ripple is a few centimetres against DW3000 per-block noise of a similar scale,
  so the per-window signal-to-noise ratio is near one. The cadence is dug out by narrowband
  gain over roughly two dozen samples, not read off directly. This is why identity (Tier 2)
  is the uncertain part and motion (Tier 1) is not.

## Two tiers

**Tier 1, motion gate (the shippable floor).** Arm hands-free only when the approach shows
a walk signature: cadence-band energy plus a closing trend in the seconds before the unlock
band is crossed. Walking versus stationary is trivially separable at this sample count
because a stationary channel has no cadence peak and its residual barely moves. On a real
capture the walking residual RMS ran 11 to 21 cm against a 2 cm gate.

**Tier 2, carrier identity (the research question).** Per-credential statistics learned from
successful arrivals (cadence, stride regularity, closing speed, deceleration, carry-height
range bias). A mismatch disarms hands-free for that one arrival. Whether real household
members separate well enough is what E1 decides.

**Design rule, non-negotiable: a mismatch never denies.** It only reverts the arrival to
tap-to-unlock. Crutches, a toddler on one hip, a wheelchair, a bike: the lock stays an
ordinary Aliro lock. Cold start equals today's behaviour. The feature only ever tightens
hands-free; it never loosens anything. That is the whole rollout-safety story, and the
accessibility guarantee.

## The tool

`tools/aliro_gait.py` reads Aliro Lab captures and reports the carry-motion features and a
per-approach verdict (CARRY+ carrying and approaching, carry carrying, still static). With
two or more labels it also runs leave-one-out nearest-centroid classification, the
pre-registered Tier-2 bar being 80 percent. It is stdlib-only and reuses the Aliro Lab
parser.

Capture and score:

```
# in ports/esp32/apps/matter-lock, on an ESP32-S3 + DWM3000EVB flashed with a lab build
make term LOG=walkup-1.log           # logs the serial port to a file
#   at the console: `lab on`, walk up several times from 4-5 m back, `lab off`

# from the repo root
python3 tools/aliro_gait.py carrier1=walkup-1.log carrier2=walkup-2.log -o e1.html
```

Each walk-up must start far enough back that the first logged range reads a couple of
metres, not the doorstep: ranging only begins after BLE authentication, so a phone already
at the door yields no approach to analyse. A carrier who walks up repeatedly without the
phone disconnecting produces one BLE session holding several approaches; the tool splits
each session on its relock stamps and scores every approach, so one held session gives many
samples.

The estimator: quadratic detrend, then a first-difference gain-compensated spectrum that
suppresses the sub-band multipath wander that would otherwise out-peak the gait line, then a
coarse pick on the coincidence minimum of the two half-windows (a noise lobe rarely repeats
at one frequency in both halves), then a fine pick on the full-window spectrum. An
incremental Goertzel estimator is held to the same answer as the reference the on-target
learner will mirror.

At this SNR a single window has an irreducible outlier rate of about 10 percent, so identity
must aggregate across arrivals and weight by peak prominence. Per-arrival gait identification
is not honest here, and the tool is written to make that hard to forget.

## E1: the pre-registered bench experiment

Run against a real iPhone with unchanged firmware, `lab on`, one serial log per subject. The
pass criteria are fixed before looking at the data:

1. Three household subjects, 20 walk-ups each, same carry mode (pocket).
2. One subject, 10 in-hand plus 10 in-bag: does carry mode swamp identity?
3. Subject A carries subject B's phone, 10 walk-ups: the features must track the carrier, not
   the phone's owner.
4. Phone stationary in the relock band for 5 minutes: the hall-table control.
5. Once, log the received RAN multiplier to confirm the rate ceiling.

Pass and fail, decided now:

- Tier 1: walking versus stationary separable with zero overlap across all captures.
- Tier 2 go: leave-one-out subject accuracy at least 80 percent within one carry mode, and
  the phone-swap set classifies as the carrier at least 80 percent of the time.
- If carry mode shifts the features more than identity does, Tier 2 becomes a per-mode
  mixture or is dropped. Tier 1 ships regardless.

## Privacy

This is biometric processing even at a few hundred bytes, and the design treats it that way.
The statistics are per-credential only, never a global "who is this" model; they live on the
device, are never exported and never logged outside lab mode; they are purgeable by a console
command and by factory reset; and learning is opt-in per lock. The same residual could
identify which household member is approaching even when that is not needed. That is
deliberately not built.
