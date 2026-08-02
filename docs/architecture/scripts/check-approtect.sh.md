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
scripts/check-approtect.sh              # both layers
scripts/check-approtect.sh --self-test  # prove the gate can actually fail
make verify                             # runs this as the `approtect` gate
Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
TWO LAYERS, because either one alone is a gate that passes while checking
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
The generated layer reporting "0 builds examined" is NOT a pass and is not
silent: it says so, and it is the reason the source layer is not optional.

**discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `scan_sources()`
`scripts/check-approtect.sh:83`

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
`scripts/check-approtect.sh:106`

---- layer 2: generated .config -------------------------------------------
Every image of every build tree, including MCUboot's own -- which is the whole
reason this layer exists now. MCUboot is a config file written from scratch,
so it is the most likely place for the setting to appear, and it is not
covered by any grep of the application's sources.
