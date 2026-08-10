# HITL: Aliro ADDR filter prove-out

## Goal
Phone-filtered `WR1 … addr=…` with separable `outside_minus_inside` by **pose**.

## A — static pose captures (done when both PASS)

```bash
# OUTSIDE — unlock, stay at outside stick for ADDR hold
python3 ~/scripts/side_hitl.py --pose outside_pause --label outside_static \
  --addr-hold-s 45 --out ~/captures/pose_out.jsonl --ports …

# INSIDE — unlock, stay inside for ADDR hold
python3 ~/scripts/side_hitl.py --pose inside_pause --label inside_static \
  --addr-hold-s 45 --out ~/captures/pose_in.jsonl --ports …
```

```bash
python3 tools/side-capture/analyze_addr.py captures/pose_out.jsonl --phase outside_pause
python3 tools/side-capture/analyze_addr.py captures/pose_in.jsonl --phase inside_pause
python3 tools/side-capture/replay_side_gate.py captures/pose_out.jsonl captures/pose_in.jsonl
```

Desk result: outside mean `omi≈+8`, inside `omi≈−18`; gate replay unlocks outside only.

## B — fail-closed lock + feed path

```bash
make build SIDE=1 CDK_BUILD=build/cdk-side
make flash CDK_BUILD=build/cdk-side
```

With `SIDE=1`, passive unlock is withheld until SF1 features are fed:

```
SF1 in=<dbm> out=<dbm> th=<dbm> ni=<n> no=<n> nt=<n>
```

Lab ingest is RTT down-buffer 0 (`CONFIG_WOZ_SIDE_FEED_RTT`). `side_hitl.py`
now feeds it by default: it holds the probe with `JLinkExe` and uses SEGGER's
bidirectional RTT server on port 19021, reading `SIDE peer=` and writing SF1 on
the one connection. `JLinkRTTLogger` cannot do this — it is up-buffer only, and
it takes the probe exclusively so no second writer can exist.

SF1 goes out only while an ADDR filter is set. Unfiltered witness means describe
every advertiser in range, so feeding them would answer confidently about the
wrong device.

```bash
python3 side_hitl.py --pose outside_pause --simulate-gate --ports …
# >> SF1 in=-52 out=-36 th=-49 ni=4 no=3 nt=5   (host → lock)
# RTT side feed: side=1 conf=…                  (lock's own verdict)
# GATE … unlock=YES|no                          (host mirror, --simulate-gate)
```

Compare the two: `GATE` is the host's mirror of the algorithm, `side feed:` is
the lock actually deciding. They should agree.

Read-only fallbacks (no SF1): `--rtt-logger`, or `--rtt-log <path>`.

## Two things that cost a whole session

**A dongle's role is firmware, not position.** Replugging USB ports changes
nothing. If the dongle standing at the outside post was flashed `role=threshold`,
`omi` collapses to ~0 and every window classifies THRESHOLD. Check it by holding
the phone against a dongle and watching which role's mean spikes ~30 dB:

```bash
grep -oE "role=[a-z]+ +obs=[0-9]+ +n=[0-9]+ +mean= *-?[0-9]+" run.log | tail -12
```

Correct it without reflashing three dongles:

```bash
--role-map ttyACM1=outside,ttyACM2=threshold
```

**The first approach usually cannot unlock.** Witnesses only learn the phone's
rotating address when the CoC opens and the lock prints `SIDE peer=`, but
committing a side needs `agree_windows`(3) × ~2 s of filtered windows. The
unlock decision arrives first, so you get one `side feed:` line and then
`passive unlock withheld: side=0`. `--addr-hold-s` keeps the filter alive across
the disconnect, so the *second* approach within the hold arrives with the side
already committed. Measured: first approach withheld, second granted.

Production witness→lock transport (UART/Matter) is still next.

## Probe and witnesses on different machines
The lock's J-Link and the witness dongles need not share a host. Run `JLinkExe`
beside the probe, forward the port, and attach from the witness side:

```bash
# machine with the lock
ssh -R 19021:127.0.0.1:19021 <pi>
# machine with the dongles
python3 side_hitl.py --rtt-attach --ports /dev/ttyACM1 /dev/ttyACM2 /dev/ttyACM3 …
```

## CDK console is RTT
`SIDE peer=` is SEGGER RTT (J-Link), not a Pi `ttyACM*`.
