<!-- generated documentation — edit the source, not this file -->
# `scripts/twin-suite.sh`

The web-twin suite for the umbrella runner (make check): the constant-drift
gate (always) plus the WASM twin's node self-test against the committed
web-twin/twin.js (when node is present). No rebuild here — regenerating
twin.js needs a pinned emsdk and is CI's byte-diff staleness gate; this only
proves the committed firmware artifact still passes its scenario.
