#!/usr/bin/env bash
#
# flash.sh — program both cores of the openaliro nRF5340 DK firmware with nrfutil.
#
# Goes over the DK's on-board J-Link. See FLASH.md for setup and first run.
#
# Usage:  bash flash.sh [JLINK_SERIAL_NUMBER]
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
[ -f merged.hex ] && [ -f merged_CPUNET.hex ] || {
  echo "ERROR: merged.hex / merged_CPUNET.hex not found next to this script."
  exit 1
}

# ---- is this the firmware we published? --------------------------------------
# Two questions, in order of how much they prove. This bundle shipped with no
# check of either kind, and its SHA256SUMS.txt did not exist at all.
#
# SHA256SUMS.txt answers "did the download arrive intact". It cannot answer
# "who built this": it travels in the same zip as the files it describes, so
# whoever could alter the images could alter the sums in the same motion. A
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
# checks that signature against openaliro's CI identity. Both cores are checked:
# the radio image is as much a part of this lock as the application one.
#
# NOT fatal, on purpose: a failure here is indistinguishable from being offline,
# unauthenticated or behind a proxy, and refusing to flash for those would be
# wrong. It is printed loudly instead, with the command to run deliberately.
if command -v gh >/dev/null 2>&1; then
  if gh attestation verify merged.hex --repo openaliro/openaliro >/dev/null 2>&1 &&
    gh attestation verify merged_CPUNET.hex --repo openaliro/openaliro >/dev/null 2>&1; then
    echo "==> provenance OK: both images were built by openaliro's CI"
  else
    echo
    echo "  NOTE: could not confirm where these images came from. That is expected offline."
    echo "  To check them deliberately:"
    echo "    gh attestation verify merged.hex --repo openaliro/openaliro"
    echo "    gh attestation verify merged_CPUNET.hex --repo openaliro/openaliro"
    echo
  fi
else
  echo "==> provenance not checked (no GitHub CLI). README.txt shows how."
fi

SNR=()
[ $# -ge 1 ] && SNR=(--serial-number "$1")

echo "==> network core (merged_CPUNET.hex)"
nrfutil device program --firmware merged_CPUNET.hex --core network \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_NONE \
  ${SNR[@]+"${SNR[@]}"}

echo "==> application core (merged.hex)"
nrfutil device program --firmware merged.hex --core application \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM \
  ${SNR[@]+"${SNR[@]}"}

echo "==> done. Open the DK's serial port at 115200 baud for the commissioning QR code."
