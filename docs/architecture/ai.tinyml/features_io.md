<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/features_io.py`

Read and write labelled feature sets, as `.npz` or as `.csv`.

Both formats carry the same four arrays: `X` (n x f float64), `y` (n int labels,
0 clear / 1 blocked), `names` (f feature names) and `frame_len` (n int, the RFRAME
length each reception came from, kept so a set can be re-filtered without
re-parsing the logs).

CSV EXISTS BECAUSE OF THE `mal-diff` GATE, not for convenience. `security/` blocks
any binary blob entering the tree, on the grounds that semgrep cannot parse it and
a reviewer cannot read it, and that is the correct call. But the captures the
shipped model is trained on are NOT regenerable from a public download the way the
eWINE set is: their only other copy is capture logs on one laptop. So the set that
`modules/woz_ml/` is fitted on lives in the tree as text, and stays reviewable.

The `.npz` path is still the default for the eWINE-derived sets, which are large,
regenerable, and gitignored.

**used by** [`ai/tinyml/codesize.py`](codesize.md), [`ai/tinyml/gen_model.py`](gen_model.md), [`ai/tinyml/leaf_bits.py`](leaf_bits.md), [`ai/tinyml/parse_alab.py`](parse_alab.md)

## API

### `write_features(path, X, y, frame_len, names)`
`ai/tinyml/features_io.py:25`

Write to `path`, choosing the format from its extension.

**called by** `main`

### `read_features(path)`
`ai/tinyml/features_io.py:45`

Return `(X, y, names, frame_len)` from a `.npz` or `.csv` written above.

**called by** `main`, `main`, `run`
