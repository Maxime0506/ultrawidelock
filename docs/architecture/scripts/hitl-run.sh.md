<!-- generated documentation — edit the source, not this file -->
# `scripts/hitl-run.sh`

hitl-run.sh — one unattended pass of the DK-as-iPhone loop: enrol the
initiator against the live reader, build it, flash it, and judge the result
off what both boards actually say.
Usage:
scripts/hitl-run.sh                       # the whole loop
scripts/hitl-run.sh --skip-enrol          # reuse src/bench_identity.h as-is
scripts/hitl-run.sh --skip-enrol --skip-build --skip-flash
# judge the boards as they run now
TIMEOUT=180 scripts/hitl-run.sh           # give the verdict window longer
PORT=/dev/cu.usbmodemXXXX scripts/hitl-run.sh   # name the DK console port
Options:
--skip-enrol   keep the existing bench_identity.h. Without this flag a run
MINTS A FRESH CREDENTIAL, which unbinds any DK image built
against the previous header -- that is why --skip-build and
--skip-flash are refused unless enrolment is skipped too:
a fresh credential with a stale board can never pass.
--skip-build   do not rebuild the initiator
--skip-flash   do not touch the DK (implies the image on it is current)
--no-reader    judge from the DK console alone, without the reader's RTT
Environment: NODE (0x1234), STORAGE (~/.aliro-chip-tool), FABRIC (alpha),
TIMEOUT (verdict window seconds, 120), PORT (DK console)
Exit: 0 PASS · 1 FAIL (verdict window closed without the markers, or a fail
marker appeared) · 2 a stage refused to run (reason on stderr)
What PASS means, and why these markers:
DK console  "=== ESTABLISHED: URSK agreed with the reader ===" -- the whole
BLE Access Protocol succeeded against the enrolled identity.
DK console  "Pre-POLL #" -- M1..M3 negotiated AND the transmitter armed; a
repeating needle on purpose, the 5 Hz cadence outlives the
serial capture's occasional byte drops where a one-shot
banner did not.
reader RTT  "PREPOLL OK" -- the reader DECRYPTED a Pre-POLL whose CCM* MIC
is keyed by mUPSK1 from the URSK (ccc_shim_rx.c), so the key the
protocol produced is the same key on both ends, on air. This is
the one marker a BLE-only fake cannot produce, which is why the
reader watch is on by default and --no-reader is the opt-out.
The reader is watched over its J-Link (firmware/keys/cdk-probe) against the
ELF recorded at deploy time (build/cdk-deployed/zephyr.elf) -- the build tree
holds the image being built NEXT, not the one running, per mk/cdk.mk.
Artifacts land in build/hitl-run/<timestamp>/: enrol.log, dk-serial.log,
cdk-rtt.log, verdict.txt. The serial capture starts BEFORE the flash so the
boot banner and a fast session are never missed.

<details><summary>Undocumented (2)</summary>

- `die`
- `cleanup`

</details>
