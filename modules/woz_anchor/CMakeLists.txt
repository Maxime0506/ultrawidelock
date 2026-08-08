# Two-anchor geometry and fusion as a Zephyr module.
#
# Contributes nothing unless CONFIG_WOZ_ANCHOR=y, and nothing sets that yet: the
# logic is finished and host-tested ahead of the hardware, so every existing
# image stays byte-identical. Wiring up the satellite transport is what
# turns it on, and that step owes a RAM budget against the CDK Matter image.
#
# Pure integer arithmetic over caller-owned structs. No allocation, no threads,
# no platform dependency -- so it links anywhere and the host suite is the whole
# correctness story until real distances exist.

if(CONFIG_WOZ_ANCHOR)

  zephyr_library_named(woz_anchor)
  zephyr_library_sources(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/woz_door.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/woz_fusion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/woz_report.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/woz_satellite.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/woz_slam.c)
  # The LIS2DH12 backend of woz_slam_hw.h (CONFIG_WOZ_ANCHOR_SLAM) is the one
  # file that was not portable integer logic; it compiles from ports/zephyr
  # (woz_zephyr_port) under that same gate.
  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

endif()
