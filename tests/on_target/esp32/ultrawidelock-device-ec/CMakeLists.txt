# Standalone ESP-IDF app: run the Aliro device (initiator) self-test on-chip
# against the real PSA P-256 backend (ultrawidelock_prim_psa.c), not the host fake curve.
# Flash to any ESP32-S3, read the serial console for PASS/FAIL. No BLE/UWB/iPhone.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ultrawidelock_device_ec_test)
