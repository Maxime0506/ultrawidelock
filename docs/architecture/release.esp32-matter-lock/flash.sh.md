<!-- generated documentation — edit the source, not this file -->
# `release/esp32-matter-lock/flash.sh`

flash.sh — write the openaliro ESP32 Matter lock to a board with esptool.
One merged image (bootloader, partition table and app) at offset 0x0. See
FLASH.md for wiring and first run.
Usage:  bash flash.sh [--chip esp32s3|esp32c5|esp32c6] [PORT]
bash flash.sh                    ask which chip, let esptool find the port
bash flash.sh --chip esp32c6     no question
bash flash.sh --chip esp32s3 /dev/ttyACM0
The bundle ships an image for each of three chips, and writing the wrong one
gives a board that flashes cleanly and then never boots. So the chip is asked
for rather than assumed: this script used to hardcode the S3 and ignore the
other two images entirely.
