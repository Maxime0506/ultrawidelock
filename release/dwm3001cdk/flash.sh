#!/usr/bin/env bash
#
# flash.sh — program the openaliro DWM3001CDK firmware over the board's
# on-board J-Link. See FLASH.md for the full walkthrough.
#
# Usage:  bash flash.sh [JLINK_SERIAL_NUMBER]
#
# One image, not two: the nRF52833 is a single-core part, so unlike the
# nRF5340 DK there is no separate network-core hex to write.
set -euo pipefail
cd "$(dirname "$0")"

command -v nrfutil >/dev/null 2>&1 || {
  echo "ERROR: nrfutil not found."
  echo "  Install it from https://www.nordicsemi.com/Products/Development-tools/nRF-Util"
  echo "  then run:  nrfutil install device"
  exit 1
}
nrfutil device --help >/dev/null 2>&1 || {
  echo "ERROR: the nrfutil 'device' plugin is missing. Run:  nrfutil install device"
  exit 1
}
[ -f merged.hex ] || {
  echo "ERROR: merged.hex not found next to this script."
  exit 1
}

# Checksums ship with the bundle; verify when we can rather than assuming. A
# corrupted download that still flashes is a board that fails in the field
# instead of failing here.
if [ -f SHA256SUMS.txt ]; then
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c SHA256SUMS.txt --ignore-missing >/dev/null 2>&1 \
      || echo "WARNING: checksums do not match. Re-download before trusting this."
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c SHA256SUMS.txt --ignore-missing >/dev/null 2>&1 \
      || echo "WARNING: checksums do not match. Re-download before trusting this."
  fi
fi

SNR=()
[ $# -ge 1 ] && SNR=(--serial-number "$1")

# ERASE_ALL is right for a first flash and is what this script is for: it takes
# any previous pairing with it, so the board comes up ready to be added to Home.
echo "==> writing merged.hex"
nrfutil device program --firmware merged.hex --core application \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM \
  ${SNR[@]+"${SNR[@]}"}

echo
echo "==> done."
if [ -f VERSION.txt ] && grep -q 'SETUP CODE' VERSION.txt; then
  echo
  grep 'SETUP CODE' VERSION.txt
  echo
  echo "    Home -> + -> Add Accessory -> More options... -> Enter Code"
else
  echo "    Your setup code is in VERSION.txt."
fi
