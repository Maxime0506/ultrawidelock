# Presence

Turn the lock into a proof. A presence assertion is a signed statement that a
named human's phone was within a few tens of centimetres of this board, at a
distance measured by UWB time of flight, checkable later by someone who has
neither the board nor the phone.

The distance is the point. BLE signal strength can be relayed; time of flight
cannot be shortened, because an attacker cannot make light arrive early. And
unlike a hardware token, it cannot be left plugged in: it measures how far away
the person is, right now.

## What it is for

Rare, high-value, deliberate actions. Signing a release. Approving a production
deploy. A key ceremony that should need two people in one room.

It is not for anything frequent or ambient. Every check costs a few seconds and
depends on the phone answering, which it does not always do. Building `sudo` on
this would be miserable.

| Side | Needs | Runs |
|---|---|---|
| Signing | ESP32-S3 + DWM3000EVB, a provisioned iPhone, this firmware | On your desk |
| Verifying | `openssl` and the committed `.presence/enrolled` | Anywhere, including CI |

That asymmetry is the design. Verification never reads a key from the thing
being verified, so a tag cannot introduce a device nobody vetted.

## Before you start

* A matter-lock board that your iPhone has been commissioned against. Presence
  runs on the provisioned board itself, so there is no identity cloning and no
  second board to set up.
* Exactly one Aliro credential provisioned on it. See
  [One credential, not two](#one-credential-not-two).
* `pyserial` on the host: `pip install pyserial`, or run the tool under
  `uv run --with pyserial`. Verification alone needs none of it.

## Build and flash

From `ports/esp32/apps/matter-lock`:

```bash
make set-target       # fresh checkout only
make presence-on      # adds CONFIG_WOZ_PRESENCE=y to sdkconfig
make presence-flash   # build + flash
```

`make` sources ESP-IDF and esp-matter itself, so no `export.sh` by hand.

Presence is **additive**. The lock still locks; it also answers signed presence
challenges on the same console. `make presence-off` removes the commands again.

`presence-flash` never erases, and must not. NVS holds three things you cannot
regenerate: the Matter fabric, the Aliro trust store, and the board's P-256
signing key, which is created on first boot and persisted. Erase it and every
enrolment you have recorded becomes worthless.

> `CONFIG_WOZ_PRESENCE` is `default n`, and nothing tracked in the repo turns it
> on. `sdkconfig` is untracked, so `presence-on` does not survive a fresh clone.
> CI builds the presence path through a separate overlay (`sdkconfig.presence`)
> so it cannot rot unnoticed, but that build is for proving it compiles, not for
> flashing.

## Enrol the board

Enrolment records what a later verifier will trust: the board's public point,
and the credential id it is pinned to.

```bash
make presence-enroll NAME=desk-lock
```

Then commit `.presence/enrolled` **as its own change**. It is the trust root, so
adding a device or a human is a reviewable diff in history rather than something
a tag can assert about itself.

Re-provisioning the phone's credential issues a new one, so the pinned id
changes and enrolment has to be redone. The tool refuses to overwrite silently:
you will get `is enrolled for a different credential id; review the trust
change`. Delete the stale line deliberately, then enrol again.

## Use it

Check the board answers at all:

```bash
make presence-probe
```

Wake the phone and hold it near the board first. A good run prints
`signature VERIFIED`, a distance, and the credential id.

Sign a tag:

```bash
make presence-sign TAG=presence/1.2.0
```

This refuses to create the tag unless presence actually verifies inside the
distance threshold, so the tag cannot exist without someone having been there.

Verify one, with no hardware:

```bash
make presence-verify TAG=presence/1.2.0      # from the repo root
```

CI does this for you. `.github/workflows/presence-tags.yml` fires on tags
matching `v*` and `presence/*`.

## Thresholds

| Knob | Default | Where |
|---|---|---|
| Distance gate | 40 cm | `CONFIG_WOZ_PRESENCE_MAX_CM` (firmware), `MAXCM=` (host) |
| Proof timeout | 8000 ms | `CONFIG_WOZ_PRESENCE_TIMEOUT_MS` |

The gate is load-bearing, not decorative. A phone outside it is refused with
`E_RANGE`, and the refusal is worth testing at least once: a distance threshold
nobody has seen reject anything is a threshold nobody knows works.

## One credential, not two

Proof requires the board to hold **exactly one** provisioned credential, so that
"a human was near" names a specific human. Check it on the console:

```
aliro prov
```

You want `trust : 1/4 anchor(s)`. Re-commissioning tends to leave the old anchor
behind, giving you two, and every proof then fails with
`expected exactly one provisioned credential`. To fix, on the console:

```
aliro clear      # empties the trust store, keeps the reader identity
                 # then present the phone until the lock opens
aliro trust      # re-adds just that credential
aliro prov       # confirm 1/4
```

`aliro clear` does not de-commission the board, so no re-pairing is needed.

## What is in an assertion

115 bytes, version 3. The signed prefix carries a magic and version, the
algorithm and status bytes, the 16-byte challenge nonce, the 8-byte credential
id, the measured distance, three range-integrity fields (`range_flags`,
`sts_quality`, `trust_level`), the board's uptime, and a wall-clock field. An
ECDSA P-256 signature over that whole prefix follows.

The integrity fields are inside the signature, not alongside it, so a frame
cannot claim a distance while hiding that the STS did not correlate. A block
whose STS failed has not measured anything, and its number is exactly the one a
distance-reduction attack gets to choose.

P-256 rather than Ed25519 because Aliro credential keys are already P-256, so
this reuses curve code already validated on target instead of adding a second
curve.

Version 2 frames are refused, not reinterpreted.

## Honest limits

State these; do not bury them.

* **Relay resistance is not measured.** The time-of-flight argument is
  structural. Nobody has attacked this implementation.
* **A cloned or compromised board can assert anything.** Enrolment pins a key,
  which means a stolen key is a full compromise.
* **The tag nonce binds an artefact, not a moment.** It is derived from
  `(tag, commit)` because CI cannot mint a challenge, so an assertion proves
  someone was present when they signed *that artefact*, not that they were
  present at a particular time. `tools/presence_git.py` documents this.
* **`unix_ms` ships as `TIME_NONE`.** The board has no trusted wall clock.
* **The phone does not always answer.** A locked, screen-off phone sometimes
  completes a full transaction and sometimes times out, and the variable
  deciding which is not yet understood. Do not design anything that must
  succeed on the first try.
* **Almost nobody has a DWM3000 on their desk.** This is the real adoption
  bottleneck, not the protocol.

## When it goes wrong

| Symptom | Cause |
|---|---|
| `expected exactly one provisioned credential` | Two anchors in the trust store. See above. |
| `proof timed out; wake the phone and hold it near the reader` | No new session inside the timeout. Wake the phone, move it closer. |
| `no public key came back from 'presence pub'` | Wrong port, a board without `CONFIG_WOZ_PRESENCE`, or a serial monitor holding the port. Quit the monitor first. |
| `E_RANGE` | Working as intended: the phone was past the distance gate. |
| Verification fails on a key id nobody recognises | The board was re-flashed after an NVS erase, or you are pointing at a different board. |

A probe resets the board as it opens the serial port, so the lock reboots on
every check and its Aliro session restarts with it. Harmless on a bench, and a
reason not to run presence checks on a schedule against a door people use.
