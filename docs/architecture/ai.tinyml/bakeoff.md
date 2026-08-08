<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/bakeoff.py`

LOS/NLOS bake-off: emlearn decision trees vs an int8-quantised Keras MLP.

Run (after extract_features.py):
    ai/tinyml/.venv/bin/python ai/tinyml/bakeoff.py

Options (env vars):
    FEATURES    input .npz (default ai/tinyml/features.npz)
    OUTDIR      where generated C / .tflite land (default ai/tinyml/out)
    SEED        split + init seed (default 42)

What "bytes" means here. Only the model payload is counted, because that is the part
this script can measure exactly and architecture-independently:
  * trees   n_nodes*8 (EmlTreesNode = int8+3*int16, 8 B aligned) + n_roots*4 + n_leaves
  * MLP     len(tflite flatbuffer)
  * both    + 136 B of feature scaling constants (17 features * 2 float32)
Runtime code size (TFLM interpreter + kernels, vs emlearn's eml_trees inference) is NOT
measured here: it needs a cross-compile for the target and no ARM toolchain is installed
in this worktree. Literature puts TFLM's interpreter core near 2 KB before kernels and
emlearn's tree inference in the low hundreds of bytes, which moves the comparison further
in the trees' favour, not less.

**discussed in** [`ai/README.md`](../../../ai/README.md), [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

## API

### `emlearn_scratch()`
`ai/tinyml/bakeoff.py:80`

Run emlearn's compile-and-load from a throwaway directory.

`predict()` builds its Python-callable extension at the fixed path
`./tmp/mytree.{c,h,o}` -- `name = 'mytree'` in emlearn/trees.py under
`temp_dir='tmp'` in emlearn/common.py -- no matter what name `save()` was
given. From the repository root that leaves build spill in the tree, and two
concurrent runs sharing a directory overwrite each other's extension so one
silently loads the other's. That failure is silent and looks exactly like a
model bug: it once reported 6300/6300 mismatches that were nothing of the
kind. A private directory per process removes both problems.

**called by** `eval_trees`

### `quantise_int16(X, lo, span)`
`ai/tinyml/bakeoff.py:101`

Affine per-feature map into int16. Trees are trained on the result, so the
emlearn int16 conversion is lossless rather than a post-hoc approximation.

**called by** `main`

### `c_model_bytes(path)`
`ai/tinyml/bakeoff.py:108`

Exact payload bytes from the emlearn-generated C.

**called by** `eval_trees`  ·  **calls** `arr`

<details><summary>Undocumented (5)</summary>

- `arr`
- `eval_trees`
- `build_mlp`
- `eval_mlp`
- `main`

</details>
