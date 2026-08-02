#!/usr/bin/env bash
# cdk-dfu.sh — push a signed image to the DWM3001CDK over MCUboot serial recovery.
#
# WHY THIS IS A SCRIPT AND NOT A MAKE RECIPE. The ordering below is the whole
# job and a recipe got it wrong twice. MCUboot listens for an mcumgr command for
# a fixed window after reset and then boots the application; miss the window and
# the port answers nothing, which looks exactly like broken wiring. So the probe
# loop has to already be RUNNING when the reset lands. Backgrounding the reset
# instead does not work: nrfjprog spends seconds connecting to the probe before
# it pulls the line, by which time the window has opened and shut.
#
# THERE IS NO BUTTON TO PRESS. The board DTS says SW1 (P0.18) "by default is
# nRESET", but that default needs CONFIG_GPIO_AS_PINRESET to write the UICR bit
# and this application does not set it. P0.18 is a plain GPIO here and SW1 resets
# nothing. Measured 2026-08-02: 180 probes across repeated SW1 presses, zero
# answers; one SWD reset with a probe loop already running answered immediately.
# So this resets over SWD and asks the operator for nothing.
#
# Single slot: the upload OVERWRITES the running image. A torn transfer leaves no
# application, which is recoverable and not a brick -- MCUboot stays in recovery
# (CONFIG_BOOT_SERIAL_NO_APPLICATION=y) and `make flash` over SWD always works.
# What a torn transfer must never be answered with is an erase: `make flash-erase`
# and `nrfjprog --recover` both destroy settings_storage, and with it the Matter
# fabrics and the reader identity.
set -uo pipefail

PORT="${1:?usage: cdk-dfu.sh <port> <baud> <signed-image> [chip]}"
BAUD="${2:?}"
IMAGE="${3:?}"
CHIP="${4:-nRF52833_xxAA}"
CONN="dev=${PORT},baud=${BAUD}"
PROBES="${DFU_PROBES:-120}"

command -v mcumgr >/dev/null 2>&1 || {
	echo "  mcumgr not found  ·  install: make tools-install" >&2; exit 1; }
[ -f "$IMAGE" ] || { echo "  no signed image at $IMAGE  ·  run \`make build\` first" >&2; exit 1; }

probe() { mcumgr --conntype serial --connstring "$CONN" -t 0.4 image list >/dev/null 2>&1; }

printf '  upload %s\n      to %s @ %s\n' "$IMAGE" "$PORT" "$BAUD"

# Already sitting in recovery from an earlier run? Serial recovery is sticky
# once it has taken a command, so skip the reset rather than kick it out.
if probe; then
	printf '  MCUboot is already in recovery\n'
else
	printf '  resetting over SWD (no button to press), probing while it comes up\n'
	HIT="$(mktemp)"; rm -f "$HIT"
	(
		i=0
		while [ "$i" -lt "$PROBES" ]; do
			i=$((i + 1))
			if probe; then printf '%s' "$i" >"$HIT"; exit 0; fi
		done
	) &
	loop=$!
	sleep 1                       # let the loop get ahead of the reset
	nrfjprog --reset >/dev/null 2>&1 || probe-rs reset --chip "$CHIP" >/dev/null 2>&1 || {
		echo "  could not reset over SWD  ·  is the probe attached (J9)?" >&2; kill $loop 2>/dev/null; exit 1; }
	wait $loop
	if [ ! -s "$HIT" ]; then
		rm -f "$HIT"
		echo "  MCUboot never answered across $PROBES probes spanning an SWD reset." >&2
		echo "  Widen CONFIG_BOOT_SERIAL_WAIT_FOR_DFU_TIMEOUT in firmware/sysbuild/mcuboot.conf," >&2
		echo "  then \`make build && make flash\`." >&2
		exit 1
	fi
	printf '  MCUboot answered on probe %s\n' "$(cat "$HIT")"; rm -f "$HIT"
fi

printf '  uploading %s bytes  ·  DO NOT INTERRUPT (60-90 s at %s baud)\n' \
	"$(wc -c <"$IMAGE" | tr -d ' ')" "$BAUD"
if ! mcumgr --conntype serial --connstring "$CONN" -t 30 image upload "$IMAGE"; then
	echo "  upload failed mid-transfer  ·  the board is still in recovery, just re-run" >&2
	exit 1
fi
printf '  upload OK  ·  resetting into it\n'
mcumgr --conntype serial --connstring "$CONN" -t 5 reset >/dev/null 2>&1 || true
