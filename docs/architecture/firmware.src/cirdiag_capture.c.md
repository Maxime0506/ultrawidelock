<!-- generated documentation — edit the source, not this file -->
# `firmware/src/cirdiag_capture.c`

@file cirdiag_capture.c — unattended CIR capture cycle for the DWM3001CDK.
Arm the CIR dump, hold it for a window, disarm it so the ring drains to the
console, wait, repeat. Forever, from boot.
WHY THIS IS NOT A SHELL COMMAND. modules/woz_uwb documents the capture as
`aliro cir dump on`, walk up, `aliro cir dump off`, which is exactly right on
a board with a console. This one has none: RTT is output-only, uart0 belongs
to the J-Link OB, the USB CDC console comes up only in provisioning mode
(where the radio never starts), and firmware/prj.conf sets
CONFIG_WOZ_UWB_SHELL=n. The single button is already the factory reset when
held through reset, and a capture image that could erase an Apple Home
credential on a mistimed press is worse than no capture image.
WHAT THE OPERATOR DOES INSTEAD. Watch `make monitor`. Each cycle prints a
`cir.cycle: n=<i>` marker at both ends of a labelled interval; everything
between them is one capture with one label. Walk up from outside during odd
cycles and from inside during even ones, or simply note the cycle numbers, and
the labelling problem is solved without the board knowing anything about doors.
TWO MODES. With CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOWS the cycle arms the
windowed-CIR dump and drains the ring, which is the full capture and which, on
this board, stops the responder transmitting entirely (measured: tx0, no range,
no unlock). Without it the summary path alone runs and the markers only bracket
intervals — one `[ALAB] ev=uwb.diag` line per reception, no accumulator read.
That is the mode worth using: ai/tinyml/RESULTS.md Result 7 measures the whole
tap-derived half of the feature set at 0.14 accuracy points.
The markers deliberately do NOT carry the [ALAB] prefix, so tools/aliro_lab.py
ignores them, the same choice uwb_cirdiag_probe() makes for its output.

## API

### `static void cirdiag_capture_thread(void *a, void *b, void *c)`
`firmware/src/cirdiag_capture.c:46`

Capture loop: arm the dump, hold it open, disarm to drain the ring, idle, repeat forever.
Never returns.
