<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/variance.py`

Does round-to-round variance carry obstruction information on this board?

Run from the repository root:
    ai/tinyml/.venv/bin/python ai/tinyml/variance.py

Then ai/tinyml/variance_significance.py, which tests whether the best gain here
survives a bootstrap and whether the shipped model already encodes it. Result 13
in RESULTS.md is what these two produced.

THE HYPOTHESIS UNDER TEST, from an external proposal: a torso shadow flickers
because a standing person sways and breathes, while a door is rigid, so the
round-to-round variance of the channel should separate body-obstruction from
door-obstruction where the instantaneous features cannot.

WHAT THIS DATA CAN AND CANNOT SAY ABOUT IT. There is no door in these captures.
`clear` is the phone in hand facing the reader; `obstructed` is the phone behind
the back or in a back pocket. Both classes are a body. So this cannot test
body-versus-door directly. What it CAN test is the premise the idea rests on:
that variance carries obstruction information at all. And the test is biased
TOWARDS the hypothesis, because the obstructed class here is exactly the
flickering case the proposal says variance detects. A null result is therefore
strong evidence against; a positive result is weak evidence for, and would need
a real door to confirm.

TWO TRAPS THIS AVOIDS.

1. Motion. The subject walked: distance sweeps 0.6 m to 5.8 m, with a median
   change of 33 cm between consecutive receptions. Raw variance of received
   power would mostly measure the walk. So the variance is taken over
   `fp_resid`, which already has free-space spreading at the measured range
   removed, and a distance-matched control is reported beside it.

2. Window leakage. Rolling features make neighbouring rows share inputs, so a
   random train/test split lets the test set peek at its own neighbours through
   the window. Every split here holds out CONTIGUOUS blocks instead. The
   difference is not cosmetic: the same features score far higher under a random
   split, and that number would be fiction.

A SECOND HYPOTHESIS, from the same proposal, is tested at the bottom: subtract a
causal running mean of `fp_resid` from itself, so a session-wide offset cancels
and the classifier sees only the shortfall below the reader's own recent normal.
Result 10 already ruled out running baselines built from ABSOLUTE power, and its
reason was mechanical rather than empirical: such a baseline anchors to the
closest recent reception, so the shortfall mostly re-encodes distance. The
question here is whether that reason survives being applied to `fp_resid`, which
has range already subtracted and therefore cannot re-encode it. Read the section
header there for what this data can and cannot say about it.

**discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

## API

### `rolling_std(x, w)`
`ai/tinyml/variance.py:78`

Trailing standard deviation over w samples, NaN until the window fills.

Trailing and not centred: a centred window would use receptions from the
future, which a lock does not have.

### `rolling_absdiff(x, w)`
`ai/tinyml/variance.py:90`

Mean absolute round-to-round change over w samples. Robust cousin of the
standard deviation, and the one a fixed-point implementation would pick.

### `causal_shortfall(x, w)`
`ai/tinyml/variance.py:100`

x minus the mean of the w receptions BEFORE it. NaN until w have passed.

Strictly causal and excluding the current sample: the question the feature
asks is "how far below its own recent normal is THIS reception", and a
baseline that includes the reception blends the signal into its own
reference and shrinks it.

**called by** `stream_shortfall`

### `interleave(a, b)`
`ai/tinyml/variance.py:114`

Round-robin two index runs proportionally, so neither ends as a solid tail.

**called by** `streams_mixed`

### `streams_single_class(groups)`
`ai/tinyml/variance.py:121`

One stream per block: the baseline only ever sees one class.

**called by** `shortfall`

### `streams_mixed(groups)`
`ai/tinyml/variance.py:126`

One stream per block PAIR, k-th clear block interleaved with k-th obstructed.

Result 10 established this control as mandatory rather than optional. Over a
single-class stream the baseline anchors to that class's own level and
normalises away precisely the thing the feature is meant to detect, so a
null there is uninformative about a deployed reader, which meets both
classes. The pair is also the held-out unit below, so a baseline never spans
the train/test split and no window has to be dropped for leakage.

**called by** `shortfall`  ·  **calls** `interleave`

### `stream_shortfall(x, streams, w)`
`ai/tinyml/variance.py:142`

causal_shortfall restarted at the head of every stream.

Restarting is what makes this leak-free without clean_window(): a baseline
that ran across a stream boundary would be computed partly from rows on the
other side of the split, which is the same leak the blocked CV exists to
stop, wearing a different hat.

**called by** `shortfall`  ·  **calls** `causal_shortfall`

### `blocked_groups(label)`
`ai/tinyml/variance.py:162`

Contiguous blocks within each label run, as GroupKFold groups.

**called by** `main`

### `clean_window(v, groups, w)`
`ai/tinyml/variance.py:174`

Rows whose whole window lies inside one block, and is not NaN.

A window that straddles a block boundary mixes a test row with training
rows, which is the leak the blocked split exists to prevent. Dropping those
rows costs (w-1) samples per boundary and removes the leak entirely.

**called by** `main`

### `score(X, y, groups, model='tree')`
`ai/tinyml/variance.py:188`

Balanced accuracy under GroupKFold over contiguous blocks.

**called by** `main`, `shortfall`

### `shortfall(d, y, groups, base_X)`
`ai/tinyml/variance.py:270`

Does subtracting a causal running mean of fp_resid buy anything?

WHAT THIS DATA CANNOT SAY, and it has to lead. The feature's purpose is to
cancel a SESSION-wide offset -- the 2.9 dB/h drift of Result 9 -- and the
tracked CSV carries no session column, so the honest test, holding out a
session, cannot be run here at all. What is run is a proxy: contiguous
blocks stand in for sessions. A block is minutes of one walk, not an hour
apart, so it contains far less drift than the thing the feature targets, and
a null under this proxy is therefore weaker evidence than a null under a
real session split would be.

Both streams are reported because they fail differently and only one of them
resembles a deployed reader; see streams_mixed(). The mechanism check at the
end is the part that does not depend on the classifier at all.

**called by** `main`  ·  **calls** `cohens_d`, `score`, `stream_shortfall`, `streams_mixed`, `streams_single_class`

<details><summary>Undocumented (3)</summary>

- `load`
- `cohens_d`
- `main`

</details>
