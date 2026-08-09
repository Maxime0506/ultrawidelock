# Aliro initiator (User-Device role) on ESP32-S3 — the bench peer for our reader.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../ports/esp32/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aliro_initiator_esp32s3)
