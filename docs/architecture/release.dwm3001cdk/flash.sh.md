<!-- generated documentation — edit the source, not this file -->
# `release/dwm3001cdk/flash.sh`

flash.sh — program the openaliro DWM3001CDK firmware over its on-board J-Link.
See FLASH.md for the full walkthrough.
Usage:  bash flash.sh [JLINK_SERIAL_NUMBER]
One image, not two: the nRF52833 is a single-core part, so unlike the
nRF5340 DK there is no separate network-core hex to write.
