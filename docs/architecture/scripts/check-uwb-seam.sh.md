<!-- generated documentation — edit the source, not this file -->
# `scripts/check-uwb-seam.sh`

check-uwb-seam.sh — keep the CCC STS seam impossible to bypass.
WHAT IS BEING PREVENTED. Four decadriver entry points carry engine behaviour
that a caller must not skip (modules/woz_uwb/src/driver/uwb_seam.h):
dwt_rxenable         arming RX must first program the CCC STS for the slot
dwt_configurestsiv   loading an STS-IV must substitute the CCC STS-V
dwt_setcallbacks     registering callbacks must insert the Pre-POLL shim,
which is what warms the next block's STS at all
dwt_configure        a PHY (re)configuration is traced
A call site that reaches past the seam is SILENT on the bench: the radio still
arms, ranging still runs, the phone just never unlocks anything because the
STS never matched. That is a bad afternoon to debug, and it is exactly the
failure mode a link-time interposer used to make structurally impossible.
This gate buys that guarantee back mechanically.
scripts/check-uwb-seam.sh              # scan the tracked sources
scripts/check-uwb-seam.sh --self-test  # prove the gate can actually fail
make verify                            # runs this as the `uwb-seam` gate
Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
WHAT IS EXEMPT, and why each one is not a hole:
uwb_seam.h                 declares the helpers; the non-engine tier inlines
straight to the driver, which IS the fallback
ccc_shim_rx.c              implements woz_uwb_arm_rx. Its own self-rearm
ccc_shim_wrap.c            implements woz_uwb_set_sts_iv          sites have
uwb_rxdiag.c               implements the other two               already
port/woz_seam_stubs.c      the ESP32 half of the same two         programmed
the STS
ccc_sts.c                  the register-level key/IV packer itself, with no
production caller (host suites only)
deps/dw3000/**             the vendor decadriver: it defines these
tests/**, ports/esp32/test/**, docs/**   host doubles and prose
Adding a file here is a decision to trust it forever. Prefer calling the seam.

**discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md), [`docs/porting.md`](../../porting.md), [`ports/esp32/apps/reader/README.md`](../../../ports/esp32/apps/reader/README.md)

## API

### `seam_call_re()`
`scripts/check-uwb-seam.sh:60`

A call, not a mention: the symbol followed by an open paren. Declarations end
in `;` and are matched too -- that is deliberate, a local `extern` of a seamed
symbol is how a bypass gets written in the first place.

**called by** `scan`

### `scan_paths()`
`scripts/check-uwb-seam.sh:82`

The tree the seam covers: our own engine + port sources. deps/ and tests/ are
excluded above rather than here so the exemption list reads as one thing.

**called by** `scan`

### `scan()`
`scripts/check-uwb-seam.sh:92`

Scan source files for calls to CCC seam symbols that bypass uwb_seam.h and report findings with
their line numbers. Return 0 if no violations are found, 1 otherwise.

**calls** `scan_paths`, `seam_call_re`

### `check_helpers()`
`scripts/check-uwb-seam.sh:120`

The seam is only worth enforcing if the helpers exist to be called. A rename
that left the callers behind would otherwise pass by finding nothing.
