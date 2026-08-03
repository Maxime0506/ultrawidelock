<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-size-baseline.py`

cdk-size-baseline.py — turn a size report into the committed baseline.

    scripts/cdk-size-baseline.py --from build/cdk-matter/size-report.json                                  --out firmware/size-baseline.json
    make cdk-size-baseline

Two things happen here and both matter.

VOLATILE FIELDS ARE DROPPED. A timestamp and a build directory change on every
run, so carrying them would make the committed file churn on every refresh and
bury the numbers that actually moved in a diff nobody reads. The commit is kept:
it is what makes the record auditable.

THE GATE SETTINGS SURVIVE A REFRESH. Floor and cap are a decision about how much
headroom this board must keep, not a measurement, so re-recording the numbers
must not quietly reset them -- which is exactly how a floor ratchets down to
meet whatever the image happens to weigh today. They are only ever changed by
editing the file or passing them here explicitly.

<details><summary>Undocumented (1)</summary>

- `main`

</details>
