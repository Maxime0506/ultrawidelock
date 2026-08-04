#!/usr/bin/env bash
#
# flash.sh — write the openaliro ESP32 Matter lock to a board with esptool.
#
# One merged image (bootloader, partition table and app) at offset 0x0. See
# FLASH.md for wiring and first run.
#
# Usage:  bash flash.sh [--chip esp32s3|esp32c5|esp32c6] [PORT]
#
#   bash flash.sh                    ask which chip, let esptool find the port
#   bash flash.sh --chip esp32c6     no question
#   bash flash.sh --chip esp32s3 /dev/ttyACM0
#
# The bundle ships an image for each of three chips, and writing the wrong one
# gives a board that flashes cleanly and then never boots. So the chip is asked
# for rather than assumed: this script used to hardcode the S3 and ignore the
# other two images entirely.
set -euo pipefail
cd "$(dirname "$0")"

CHIP=""
PORT_ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    # The guard is not decoration: `shift 2` with one argument left is an error,
    # and under set -e that exits 1 with nothing printed at all.
    --chip)
      [ $# -ge 2 ] && [ -n "$2" ] || {
        echo "ERROR: --chip needs a value, for example:  bash flash.sh --chip esp32s3"
        exit 1
      }
      CHIP="$2"; shift 2 ;;
    --chip=*) CHIP="${1#*=}"; shift ;;
    -h|--help) sed -n '3,12p' "$0"; exit 0 ;;
    -*) echo "ERROR: unknown option: $1"; exit 1 ;;
    *) PORT_ARG="$1"; shift ;;
  esac
done

# ---- which chip --------------------------------------------------------------
CHIPS=""
for f in openaliro-matter-lock-*.bin; do
  [ -f "$f" ] || continue
  f="${f#openaliro-matter-lock-}"
  CHIPS="$CHIPS ${f%.bin}"
done
CHIPS="${CHIPS# }"
[ -n "$CHIPS" ] || {
  echo "ERROR: no openaliro-matter-lock-*.bin found next to this script."
  echo "  Run this from inside the unzipped bundle."
  exit 1
}

if [ -z "$CHIP" ]; then
  # shellcheck disable=SC2086  # deliberate split: CHIPS is a space-separated list
  set -- $CHIPS
  if [ $# -eq 1 ]; then
    CHIP="$1" # one image, no question worth asking
  elif [ -t 0 ]; then
    echo "Which board are you flashing?"
    echo
    i=1
    for c in $CHIPS; do
      echo "  $i) $c"
      i=$((i + 1))
    done
    echo
    printf 'Enter a number, or the chip name: '
    read -r answer
    case "$answer" in
      '') echo "ERROR: nothing chosen."; exit 1 ;;
      # A number picks by position. Anything else is taken as a chip name and
      # validated below exactly as an explicit --chip would be.
      [0-9]*)
        i=1
        for c in $CHIPS; do
          [ "$i" = "$answer" ] && CHIP="$c"
          i=$((i + 1))
        done
        ;;
      *) CHIP="$answer" ;;
    esac
  else
    echo "ERROR: this bundle holds more than one image, so the chip has to be named."
    echo "  bash flash.sh --chip <one of: $CHIPS>"
    exit 1
  fi
fi

BIN="openaliro-matter-lock-$CHIP.bin"
[ -f "$BIN" ] || {
  echo "ERROR: no image for '$CHIP' in this bundle."
  echo "  available: $CHIPS"
  exit 1
}

# ---- the tool ----------------------------------------------------------------
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
elif python3 -m esptool version >/dev/null 2>&1; then
  ESPTOOL=(python3 -m esptool)
else
  echo "ERROR: esptool not found. Install it with:  pip install esptool"
  exit 1
fi

# ---- is this the firmware we published? --------------------------------------
# Two questions, in order of how much they prove.
#
# SHA256SUMS.txt answers "did the download arrive intact". It cannot answer
# "who built this": it travels in the same zip as the files it describes, so
# whoever could alter the image could alter the sums in the same motion. A
# mismatch is fatal here — an image that is corrupt but flashes anyway is a
# board that fails in a doorway rather than on a desk.
if [ -f SHA256SUMS.txt ]; then
  SUMTOOL=()
  if command -v shasum >/dev/null 2>&1; then
    SUMTOOL=(shasum -a 256 -c)
  elif command -v sha256sum >/dev/null 2>&1; then
    SUMTOOL=(sha256sum -c)
  fi
  if [ ${#SUMTOOL[@]} -gt 0 ]; then
    if "${SUMTOOL[@]}" SHA256SUMS.txt >/dev/null 2>&1; then
      echo "==> checksums OK"
    else
      echo "ERROR: this bundle does not match its own checksums."
      echo "  Something changed after it was published, or the download is damaged."
      echo "  Re-download it. Do not flash this."
      exit 1
    fi
  fi
fi

# The provenance question, which checksums cannot answer. Every file published
# with this release is signed by the workflow that built it, and the GitHub CLI
# checks that signature against openaliro's CI identity.
#
# NOT fatal, on purpose: a failure here is indistinguishable from being offline,
# unauthenticated or behind a proxy, and refusing to flash for those would be
# wrong. It is printed loudly instead, with the command to run deliberately.
if command -v gh >/dev/null 2>&1; then
  if gh attestation verify "$BIN" --repo openaliro/openaliro >/dev/null 2>&1; then
    echo "==> provenance OK: $BIN was built by openaliro's CI"
  else
    echo
    echo "  NOTE: could not confirm where $BIN came from. That is expected offline."
    echo "  To check it deliberately:"
    echo "    gh attestation verify $BIN --repo openaliro/openaliro"
    echo
  fi
else
  echo "==> provenance not checked (no GitHub CLI). README.txt shows how."
fi

# ---- flash -------------------------------------------------------------------
PORT=()
[ -n "$PORT_ARG" ] && PORT=(--port "$PORT_ARG")

echo "==> writing $BIN to a $CHIP at 0x0"
"${ESPTOOL[@]}" --chip "$CHIP" ${PORT[@]+"${PORT[@]}"} --baud 460800 \
  write_flash 0x0 "$BIN"

echo
echo "==> done. Open the serial port at 115200 baud for the commissioning QR code."
