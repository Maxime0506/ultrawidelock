# Makefile — single command entry point for the OpenAliro firmware.
#
# A dispatcher, not an implementation: shared variables, the per-port includes
# below, and the `help` renderer. Every recipe lives in mk/<port>.mk, grouped by
# what it builds rather than by verb, so a target's neighbours are the other
# targets for the same board.
#
#   make                 # this grouped, colourised help
#   make build           # the DWM3001CDK lock (Matter over Thread) -> build/cdk-matter
#   make nrf-build       # the nRF5340 DK image                     -> build/nrf5340dk
#   make esp-build APP=matter-lock TARGET=esp32s3                   -> build/esp32-matter-lock-esp32s3
#   make test            # host suites (no toolchain, no hardware)
#
# Bare build/flash/monitor mean the DWM3001CDK because that is this project's
# headline target: one nRF52833 carrying the Aliro reader, the DW3110's ranging,
# a hand-written Matter node and an OpenThread MTD. The other two boards keep
# their own prefixes.

.DEFAULT_GOAL := help

# Must be computed before the includes: $(MAKEFILE_LIST) grows with each one,
# so `lastword` stops meaning "this file" the moment mk/*.mk is pulled in.
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# ---- one build root for the whole repo --------------------------------------
# Every producer — the three firmware ports, the host suites, the docs pipeline
# and the build signatures — derives its own subdirectory under this, so
# `make clean` is a single rm and nothing is left behind anywhere in the tree.
# Exported, because the shell scripts below read it rather than guessing.
#
#   build/cdk-matter/  cdk-reader/  cdk-selftest/
#   build/nrf5340dk/   nrf5340dk-blob/
#   build/esp32-<app>-<target>[-<variant>]/
#   build/host/        host_test* coverage/ fuzz/ cbmc/
#   build/_sig/        build signatures
#
# site/ is NOT here: it is publishable output, not an intermediate.
ALIRO_BUILD_ROOT ?= $(REPO_ROOT)/build
export ALIRO_BUILD_ROOT

# NCS version for both Zephyr ports. Matches scripts/build-nrf5340dk.sh.
NCS_VER ?= v3.3.0

# ---- options forwarded to the firmware builds -------------------------------
# Set on the command line: make nrf-build PRETTY=1.
# The in-tree Aliro stack is the default; ALIRO_SOURCE=0 selects the legacy
# Nordic archive for comparison or regression isolation.
CHIP     ?=
PRETTY   ?=
PRISTINE ?=
SELFTEST ?=
STRICT   ?=
# HA=1 opts into the Home Assistant variant. It must be set on BOTH bootstrap
# (applies the data-model patches) and build (layers woz-ha.conf); see
# integration/homeassistant/README.md. Not hardware-validated.
HA            ?=
ALIRO_SOURCE  ?=
ALIRO_TRACE   ?=
NFC           ?=
CIR           ?=

# Serial monitor (make nrf-term / make esp-monitor). PORT auto-detects; override
# PORT/BAUD if the guess is wrong, and set LOG=file.log to tee the raw stream.
PORT     ?=
BAUD     ?= 115200
LOG      ?=

# Which security gates `make security` runs. Empty means all eight.
GATES    ?=

# Presence-signed tags (make presence-verify TAG=presence/1.2.0). MAXCM is the
# distance threshold in cm; 40 matches tools/presence_verify.py's default.
TAG      ?=
MAXCM    ?= 40
PRESENCE_RUNTIME_OUT ?= $(ALIRO_BUILD_ROOT)/presence-runtime.tar.gz

# ---- the ports, in the order `make help` should introduce them ---------------
include $(REPO_ROOT)/mk/cdk.mk
include $(REPO_ROOT)/mk/nrf5340dk.mk
include $(REPO_ROOT)/mk/esp32.mk
include $(REPO_ROOT)/mk/setup.mk
include $(REPO_ROOT)/mk/host.mk
include $(REPO_ROOT)/mk/docs.mk
include $(REPO_ROOT)/mk/extras.mk
