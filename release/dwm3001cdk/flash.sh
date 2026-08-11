#!/usr/bin/env bash
#
# flash.sh — program the ultrawidelock DWM3001CDK firmware over its on-board J-Link.
#
# See FLASH.md for the full walkthrough.
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

# ---- is this the firmware we published? --------------------------------------
# Two questions, in order of how much they prove.
#
# SHA256SUMS.txt answers "did the download arrive intact". It cannot answer
# "who built this": it travels in the same zip as the files it describes, so
# whoever could alter the image could alter the sums in the same motion. A
# mismatch is fatal here — an image that is corrupt but flashes anyway is a
# board that fails in a doorway rather than on a desk. This used to warn and
# carry on, which is the same as not checking.
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
# checks that signature against ultrawidelock's CI identity.
#
# NOT fatal, on purpose: a failure here is indistinguishable from being offline,
# unauthenticated or behind a proxy, and refusing to flash for those would be
# wrong. It is printed loudly instead, with the command to run deliberately.
if command -v gh >/dev/null 2>&1; then
  if gh attestation verify merged.hex --repo ultrawidelock/ultrawidelock >/dev/null 2>&1; then
    echo "==> provenance OK: merged.hex was built by ultrawidelock's CI"
  else
    echo
    echo "  NOTE: could not confirm where merged.hex came from. That is expected offline."
    echo "  To check it deliberately:"
    echo "    gh attestation verify merged.hex --repo ultrawidelock/ultrawidelock"
    echo
  fi
else
  echo "==> provenance not checked (no GitHub CLI). README.txt shows how."
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
