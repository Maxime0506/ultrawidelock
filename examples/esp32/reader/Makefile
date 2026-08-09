# Makefile — forwarder for the standalone ESP32 Aliro reader (no Matter).
#
# The recipes live in mk/esp32.mk at the repo root, so the serial-port guard
# that refuses any SEGGER/J-Link port exists in one place and every build lands
# under the one build root. This file only supplies APP= and passes the target
# through, so the commands this app's README documents keep working from here:
#
#   make set-target TARGET=esp32c6   ->  make -C <root> esp-set-target APP=reader
#   make build                       ->  make -C <root> esp-build      APP=reader
#   make presence-clone SRC=/dev/... ->  make -C <root> esp-presence-clone APP=reader
#
# Running them from the repo root as `make esp-<target> APP=reader` is the same
# thing and is what the top-level `make help` lists.

APP  := reader
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../..)

.DEFAULT_GOAL := help

.PHONY: help
help:
	@$(MAKE) --no-print-directory -C $(ROOT) esp-help APP=$(APP)

# Forward anything else to the root as esp-<target>. `force` keeps the pattern
# rule from ever being considered already-satisfied, and the empty rules below
# stop make trying to rebuild its own makefiles through this pattern.
.PHONY: force
force: ;
Makefile: ;
%: force
	@$(MAKE) --no-print-directory -C $(ROOT) esp-$@ APP=$(APP)
