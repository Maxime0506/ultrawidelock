# Woz UWB (Aliro DS-TWR responder) engine on ESP32 — ESP-IDF port.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../ports/esp32/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ultrawidelock_uwb_esp32s3)
