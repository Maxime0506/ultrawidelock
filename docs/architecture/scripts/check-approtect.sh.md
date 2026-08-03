<!-- generated documentation — edit the source, not this file -->
# `scripts/check-approtect.sh`

check-approtect.sh — refuse to ship an image that locks APPROTECT.
WHAT IS BEING PREVENTED. On the nRF52833 (and the nRF5340), selecting
CONFIG_NRF_APPROTECT_LOCK makes SystemInit() lock the firmware branch of the
APPROTECT mechanism on EVERY boot, before any of our code runs. The only way
back is `nrfjprog --recover`, which mass-erases flash AND UICR. On this
project that is not "lose the firmware" -- it is:
* settings_storage (0x7e000) gone, so the Matter fabrics and trust anchors go
* the reader private key gone (firmware/src/prov_shell.c), and
EVERY iPhone key already provisioned against this board dies with it
A board that has done this is not bricked, but every future debug session
costs a full wipe and a re-provision, and the credentials cannot be recreated.
NCS defaults to open (NRF_APPROTECT_USE_UICR); the requirement is only that
nobody turns it on. This gate is what makes "nobody" true.
scripts/check-approtect.sh              # the two config layers
scripts/check-approtect.sh --device SNR # what the attached board is ACTUALLY in
scripts/check-approtect.sh --self-test  # prove the gate can actually fail
make verify                             # runs this as the `approtect` gate
Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
THREE LAYERS, because any one alone is a gate that passes while checking
nothing:
sources    Every tracked config file. This is the layer that works in CI,
which never builds firmware (firmware-builds.yml is
workflow_dispatch only), so a .config scan there would find zero
files and report success against nothing.
generated  Every */zephyr/.config that exists locally. This is the layer
that catches what the source scan CANNOT: the setting arriving
from a board defconfig, an SoC Kconfig default, or a sysbuild
set_config_bool -- none of which appear anywhere in this tree.
Checking the generated config is the only way to know what was
actually compiled, which is why the source layer never stands in
for it.
device     What the SILICON is in right now, read back over the probe.
Opt-in (--device SNR) because it needs a board attached.
The generated layer reporting "0 builds examined" is NOT a pass and is not
silent: it says so, and it is the reason the source layer is not optional.
WHY THE DEVICE LAYER EXISTS, which is the expensive lesson. Both config layers
answer "did our firmware ask for the lock". Neither answers "is this board
locked", and those come apart: a mass erase leaves UICR blank, and on the
nRF5340 a blank UICR reads as APPROTECT ENGAGED until firmware writes it open
again. On 2026-08-03 an nRF5340 DK sat in exactly that state while this gate
reported "3 generated image config(s) examined, all open", which was true and
useless. The probe then served partial reads: RAM below ~0x20057000 read back
fine and everything above it returned "memory protection issue", which reads
exactly like a board with 100 KB of RAM missing. Hours went into a hardware
theory for what was a protection state, and `nrfutil device recover` cleared it
in one command. Ask the silicon.

**discussed in** [`CHANGELOG.md`](../../../CHANGELOG.md), [`firmware/README.md`](../../../firmware/README.md)

## API

### `scan_sources()`
`scripts/check-approtect.sh:99`

---- layer 1: tracked sources ---------------------------------------------
Matches an ASSIGNMENT, not a mention. The distinction is the whole difficulty
of this layer: the two files that enforce this rule -- CMakeLists.txt and
sysbuild.cmake -- both name all four symbols in an `if()` and interpolate them
into a FATAL_ERROR string, and a scan that merely greps for the symbol reports
the guard as the violation. That is not a hypothetical; it is what the first
version of this function did.
So exactly two shapes count, and nothing else does:
Kconfig fragment   SYMBOL=y            (optionally indented)
CMake              set(SYMBOL y|ON|TRUE ...)
`if(SYMBOL)`, "${SYMBOL}" and any comment are reads, not writes, and are
ignored. Anything that sets one of these for real takes one of the two forms.

### `scan_generated()`
`scripts/check-approtect.sh:122`

---- layer 2: generated .config -------------------------------------------
Every image of every build tree, including MCUboot's own -- which is the whole
reason this layer exists now. MCUboot is a config file written from scratch,
so it is the most likely place for the setting to appear, and it is not
covered by any grep of the application's sources.

<details><summary>Undocumented (1)</summary>

- `scan_device`

</details>
