# Side-of-door capture and training helpers (Raspberry Pi / host).

## side_hitl.py (preferred — one terminal)
RTT `SIDE peer=` → `ADDR` on witnesses → live `WR1` + JSONL.

```bash
python3 side_hitl.py --out ~/captures/aliro_outpause.jsonl --label outpause
```

Needs SEGGER `JLinkRTTLogger` on the Pi and CDK J-Link USB plugged in.

Phase keys (+ Enter): `o` outside_pause, `i` inside_pause, `t` threshold_pause,
`w` walking, `n` new_walk. Prefer deliberate **outside pauses** — that class was
scarce in the first desk capture.

## analyze_addr.py
Score ADDR-filtered `outside_minus_inside` (skips bad JSON; optional `--phase`):

```bash
python3 tools/side-capture/analyze_addr.py captures/aliro_outpause.jsonl
python3 tools/side-capture/analyze_addr.py captures/aliro_outpause.jsonl --phase outside_pause
```

## set_filt.py
- `LEARN` on one dongle (hold phone against it), then `FILT` that hash on all ports.
```bash
python3 set_filt.py --learn /dev/ttyACM1   # OUT stick
python3 set_filt.py --filt a1b2            # manual
python3 set_filt.py --clear
```

## aliro_bridge.py / watch_trio.py
Lower-level pieces; use `side_hitl.py` unless debugging.

## watch_trio.py
- Reads all `/dev/ttyACM*` (or named ports) at once.
- Prints live `WR1` lines tagged by port; Ctrl-C prints role→port map.
- Optional `--out` JSONL buckets by host time (~3 s), not per-board obs ids.

```bash
python3 watch_trio.py
python3 watch_trio.py --label outside_approaching --out captures/run.jsonl
```

## collect.py
- Ingests `WR1` UART lines from BLE witnesses (single `--uart`).
- Writes labelled JSONL trajectories (one file per walk).
- `--baseline` summarises outside_minus_inside by label.
- Prefer `watch_trio.py` when three dongles are plugged in.

## Dataset rules
- Label complete trajectories, not isolated static samples.
- Mark outside/inside pauses during ADDR hold (`o` / `i` in `side_hitl.py`).
- Split train/test by trajectory (and preferably by day).
- Never shuffle adjacent windows from one approach across the split.

## Privacy
- Witnesses emit no BD_ADDR and no stable phone identifiers.
- Correlate on ephemeral `obs_session_id` + timing only.
