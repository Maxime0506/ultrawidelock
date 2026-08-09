# Side-of-door capture and training helpers (Raspberry Pi / host).

## collect.py
- Ingests `WR1` UART lines from BLE witnesses.
- Writes labelled JSONL trajectories (one file per walk).
- `--baseline` summarises outside_minus_inside by label.

## Dataset rules
- Label complete trajectories, not isolated static samples.
- Split train/test by trajectory (and preferably by day).
- Never shuffle adjacent windows from one approach across the split.

## Privacy
- Witnesses emit no BD_ADDR and no stable phone identifiers.
- Correlate on ephemeral `obs_session_id` + timing only.
