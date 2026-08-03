<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-size-compare.py`

cdk-size-compare.py — head against the recorded baseline, as a gate.

    scripts/cdk-size-compare.py --baseline firmware/size-baseline.json                                 --current build/cdk-matter/size-report.json
    make cdk-size-check

Exit 0 when the image still fits with room to spare, 1 on a floor or cap
violation, 2 when there is nothing to compare, and 3 when the two reports were
not built the same way.

THREE IS NOT A SOFTER ONE. A size delta measured across a toolchain bump, an
overlay change or an LTO flip is not a delta: LTO alone is worth 41,084 B of
flash on this image (mk/cdk.mk), which would swamp every real signal in either
direction. So a configuration difference REFUSES TO PRODUCE A NUMBER rather
than producing a misleading one, and the fix is to refresh the baseline, not to
widen the cap.

THE FLOOR IS THE GATE, and it is expressed in free bytes. The CDK image runs at
roughly 95% of a 128 KB part, where a 644 B regression moves the percentage by
half a point and reads as rounding. Percentages are printed for orientation and
nothing is decided on them.

TOP MOVERS ARE A DIAGNOSTIC AND NEVER FAIL A BUILD. Under LTO the symbol names
are not stable across builds (see normalise_symbol in cdk-size.py), so the
attribution below is indicative: it tells you where to look, not what happened.

## API

### `baselines_of(doc)`
`scripts/cdk-size-compare.py:64`

Every recorded configuration, as {key: baseline}.

**called by** `main`

### `config_diff(base, cur)`
`scripts/cdk-size-compare.py:99`

Every way the two builds were not the same build.

**called by** `main`

### `movers(base, cur, limit)`
`scripts/cdk-size-compare.py:127`

Symbols whose size changed, largest absolute change first.

**called by** `main`

### `gate_region(name, base, cur, floor, cap, allow_growth)`
`scripts/cdk-size-compare.py:139`

Compare one region. Returns (row, failures).

**called by** `main`  ·  **calls** `fmt`

<details><summary>Undocumented (5)</summary>

- `load`
- `fmt`
- `signed`
- `markdown`
- `main`

</details>
