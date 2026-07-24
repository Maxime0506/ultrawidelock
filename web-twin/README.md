# web-twin: the walk-up digital twin as an interactive page

`index.html` plus `twin.js` is the whole page (no external requests): open it
straight from the filesystem or serve it from any static host. Drag the phone
toward the door, or press Walk up, and the reader's unlock pipeline reacts:
BLE connect radius, Aliro session bring-up, UWB DS-TWR ranging blocks, the
range-integrity trust gate, the approach controller, the bolt.

The scene is a 2.5D perspective view drawn on a plain canvas (no library, so it
stays self-contained): a receding floor, a lit reader module and a spring-loaded
bolt, UWB wavefronts rippling out on each 192 ms ranging block, and a drifting
RF density field that thickens while a session is active. When the page is the
site copy (`site/twin.html`) or was reached from another page, a "Docs" link in
the top bar goes back; opened on its own it has nowhere to return to, so the
link stays hidden rather than dead-linking.

The decision logic is the firmware. `twin.js` is `modules/woz_uwb` — the CCC
DS-TWR responder (`ccc_shim_rx.c`), range store and trust gate
(`fira_session.c`) and the facade unlock seam (`woz_uwb_facade.c`) — compiled
unmodified to WASM by

    make twin-wasm        # scripts/twin-wasm.sh, needs emsdk

against the same `tests/host` shim the host suite links, with
`web-twin/twin_glue.c` exporting the page's entry points and
`tests/host/twin_frames.c` (shared with `test_twin.c`) building the peer's
genuinely CCM*-encrypted frames. Every 192 ms ranging block on the page is a
full Pre-POLL/POLL/Response/Final/Final_Data exchange decoded by the
firmware's own RX state machine; the debugger panel single-steps those legs
and the firmware-console panel streams the C's own DIAG output. The build is
reproducible: CI rebuilds `twin.js` with the pinned emsdk and byte-diffs it,
so the committed firmware can never go stale.

Only the ESP32 median/dwell approach controller
(`ports/esp32/apps/matter-lock/main/app_main.cpp`) remains a line-cited
JavaScript port — it is application code, not part of `woz_uwb` — plus the
world pacing. Those constants live in the page's `FW` table with `file:line`
citations, and

    python3 web-twin/check_constants.py

fails if any cited value drifts. Environment knobs that are not firmware (BLE
radio range, noise probability, auth-phase pacing) are marked SIM in the page.
On every load the page replays the scenario `tests/host/test_twin.c` asserts
(legit 234 cm round, Ghost-Peak spoof true-reject, trust earned at K=3, per-leg
stepping) against the compiled firmware and shows the result in the footer;
`node web-twin/selftest.cjs` (`make test-twin`) runs the same replay in CI.

## Theming and the docs site

The page is themed off the same design tokens as the docs site (the warm ivory
paper, terracotta accent, and system type from `tools/docs_theme.py`), and is
fully light/dark aware: it reads
the site's own `dm-theme` preference from local storage, falls back to the OS
setting, and carries a matching theme toggle in its top bar. So it looks like
part of the site whether it is opened on its own or reached from it.

It folds into the generated site through `tools/docs_twin.py`, a repo-side
post-pass in the same style as `tools/docs_media.py`: it copies this page to
`site/twin.html` (and `twin.js` beside it) and adds one call-to-action on the
landing page linking to it. The pass runs from `make docs` (after the media
pass, before the link pass), so the generator itself is never touched.
