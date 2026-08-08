<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/`

| subsystem | about |
|---|---|
| [`ai/tinyml/bakeoff.py`](bakeoff.md) | LOS/NLOS bake-off: emlearn decision trees vs an int8-quantised Keras MLP. |
| [`ai/tinyml/codesize.py`](codesize.md) | Cross-compile the chosen tree for the CDK's core and measure what it costs. |
| [`ai/tinyml/extract_features.py`](extract_features.md) | Extract LOS/NLOS features from the eWINE UWB data set. |
| [`ai/tinyml/features_io.py`](features_io.md) | Read and write labelled feature sets, as `.npz` or as `.csv`. |
| [`ai/tinyml/gen_model.py`](gen_model.md) | Generate the shipped LOS/NLOS tree, its scaler, and the golden vectors. |
| [`ai/tinyml/leaf_bits.py`](leaf_bits.md) | Does emlearn's leaf_bits actually close the forest-vs-sklearn gap? |
| [`ai/tinyml/linear.py`](linear.md) | Does a linear boundary beat the shipped two-split tree, and can its margin be |
| [`ai/tinyml/parse_alab.py`](parse_alab.md) | Turn `make monitor` captures into the scalar feature set, one class per file. |
| [`ai/tinyml/range_bias.py`](range_bias.md) | Does an OBSTRUCTED phone read FARTHER than an unobstructed one, phone fixed? |
| [`ai/tinyml/variance.py`](variance.md) | Does round-to-round variance carry obstruction information on this board? |
| [`ai/tinyml/variance_significance.py`](variance_significance.md) | Is the best variance gain real, and is the information already in the model? |
