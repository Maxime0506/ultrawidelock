<!-- generated documentation — edit the source, not this file -->
# `scripts/build-nrf5340dk.sh`

build-nrf5340dk.sh {build|rebuild|flash|flash-erase|build-flash} — build the
Aliro NFC+UWB image for the nRF5340 DK from the self-contained ./workspace.
Run scripts/bootstrap.sh first.
Named for its board because BOARD below is hardcoded: this script builds
nrf5340dk/nrf5340/cpuapp and nothing else. The DWM3001CDK is built straight
from firmware/ by mk/cdk.mk, and the ESP32 apps by mk/esp32.mk.
Layers our modules + ISC dw3000 onto the fetched add-on via out-of-tree
overlays. Output → build/nrf5340dk (git-ignored), or build/nrf5340dk-blob
when ALIRO_SOURCE=0, so flipping that flag no longer forces a pristine rebuild.
Incremental by default — a full from-scratch (pristine) build runs only when it
has to: first build, changed build flags (UWB chip / self-test / config), or
when you ask for one. A preflight first checks the workspace is bootstrapped.
scripts/build-nrf5340dk.sh build                  # incremental where safe (fast)
scripts/build-nrf5340dk.sh rebuild                # force a clean pristine build
PRISTINE=1 scripts/build-nrf5340dk.sh build       # same as rebuild
UWB_SELFTEST=1 scripts/build-nrf5340dk.sh build   # one-shot boot self-test, no iPhone (diagnostic)
PRETTY=1 scripts/build-nrf5340dk.sh build         # curated/clean console (reversible; default verbose)
ALIRO_SOURCE=0 scripts/build-nrf5340dk.sh build   # legacy Nordic Aliro binary fallback
UWB_CHIP=dw3720 scripts/build-nrf5340dk.sh build  # select the plugged-in UWB chip (default: dw3000)
LTO=1 scripts/build-nrf5340dk.sh build            # link-time optimisation (overlays/lto.conf)
DFU=1 scripts/build-nrf5340dk.sh build            # MCUboot + Matter OTA (overlays/sysbuild-dfu.conf)
NOTE both default to OFF *here* and ON via `make nrf-build`, which is the same
split the DWM3001CDK uses: mk/ is the policy layer and decides what a plain
build means, this script only does what it is told. Call it directly and you
get neither unless you ask.

**discussed in** [`docs/configuring.md`](../../configuring.md), [`ports/nrf5340dk/README.md`](../../../ports/nrf5340dk/README.md)

## API

### `launch()`
`scripts/build-nrf5340dk.sh:85`

Launch a command in the NCS toolchain environment for the configured version.

**called by** `do_build`

### `sha()`
`scripts/build-nrf5340dk.sh:88`

Compute SHA-1 hash; tries shasum first (BSD/macOS), falls back to sha1sum (Linux). Filters output to the hash hex string only.

**called by** `do_build`

### `hdr()`
`scripts/build-nrf5340dk.sh:98`

Print a section header to stdout: blue "==>" followed by bold text. Used to mark the start of major build phases (preflight, build, done).

**called by** `do_build`, `preflight`

### `ok()`
`scripts/build-nrf5340dk.sh:100`

Print a checkmark to stdout in green followed by text. Used to mark successful completion of build steps.

**called by** `do_build`, `preflight`

### `kv()`
`scripts/build-nrf5340dk.sh:102`

Print a key-value pair indented: dim key (9 chars wide) and value. Used to display build configuration during the build phase.

**called by** `do_build`, `resolve_snr`

### `die()`
`scripts/build-nrf5340dk.sh:104`

Print an error message to stderr and exit with status 1. First line prints the error text in red; remaining arguments are printed as indented hints (dim text with arrow prefix). Used by preflight checks and build validation to fail fast on missing prerequisites or configuration errors.

**called by** `do_build`, `preflight`, `require_built`, `resolve_chip`, `resolve_snr`

### `resolve_chip()`
`scripts/build-nrf5340dk.sh:112`

Resolve UWB_CHIP -> the dw3000 decadriver's chip Kconfig choice (deps/dw3000/Kconfig).
Same DT node + wiring for both; only which *_device.c/dwt_driver builds changes.

**called by** `do_build`  ·  **calls** `die`

### `preflight()`
`scripts/build-nrf5340dk.sh:121`

Verify bootstrap.sh left everything the build needs. All cheap fs/git checks.

**called by** `do_build`  ·  **calls** `die`, `hdr`, `ok`

### `do_build()`
`scripts/build-nrf5340dk.sh:160`

Build the Aliro UWB firmware image. Runs preflight checks, resolves chip config, applies optional overlays (pretty console, latency diagnostics, self-test), computes a signature from all -D flags, and runs west build (pristine if config changed, incremental otherwise). Writes build signature to a cache file to detect future flag changes. Outputs merged.hex to BUILD directory.

**calls** `die`, `hdr`, `kv`, `launch`, `ok`, `preflight`, `resolve_chip`, `sha`

### `require_built()`
`scripts/build-nrf5340dk.sh:434`

Verify that a west build has completed in BUILD directory (build.ninja exists). Called before flash operations to fail fast if build has not run.

**calls** `die`

### `resolve_snr()`
`scripts/build-nrf5340dk.sh:442`

Resolve which J-Link probe to flash, into SNR. Only nRF5340DKs (board version
PCA10095 in nrfutil device list) qualify, so another attached probe (e.g. a
DWM3001CDK) is never a candidate. One DK -> auto-select it; several -> prompt;
none -> fail loud. The flash always names its target explicitly via --dev-id.

**calls** `die`, `kv`

### `warn_if_locked()`
`scripts/build-nrf5340dk.sh:482`

Confirm the board we just wrote is not sitting in an APPROTECT-engaged state.
This runs AFTER the flash rather than before, because a mass erase is one of
the ways a board gets into that state: `west flash --erase` blanks UICR, and a
blank UICR reads as APPROTECT ENGAGED on the nRF5340 until firmware writes it
open again. So the dangerous moment is the one immediately after this command
succeeds, when everything looks like it worked.
Failing here is a warning, not a build failure: the image IS on the board and
saying so is more useful than pretending the flash did not happen. What must
never happen is silence, because a locked board does not announce itself. It
serves PARTIAL debug reads, so RAM above some address starts returning
"memory protection issue" and the board reads as physically broken.
scripts/check-approtect.sh owns the actual test, including its self-test. This
is a call site, not a second implementation.
