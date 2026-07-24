<!-- generated documentation — edit the source, not this file -->
# `scripts/twin-wasm.sh`

Build the web twin's firmware: modules/woz_uwb + the tests/host shim compiled
to WASM (Emscripten), driven by web-twin/twin_glue.c. Output is a single
self-contained web-twin/twin.js (MODULARIZE + SINGLE_FILE: the .wasm rides
embedded, so the page keeps working from file:// and the site copy stays a
flat file pair). The compile is path-prefix-mapped for reproducibility: the
same emsdk version must produce a byte-identical twin.js on any machine,
which is what lets CI rebuild and diff it as a staleness gate.

**discussed in** [`web-twin/README.md`](../../../web-twin/README.md)
