# Hand-written minimal Matter node as a Zephyr module.
# Contributes nothing unless CONFIG_WOZ_MATTER=y (every existing build stays
# byte-identical). See internal/cdk-matter-plan.md for why this exists at all:
# CHIP does not fit the nRF52833 and rs-matter cannot supply a Door Lock
# cluster, so the DWM3001CDK can only self-provision from Apple Home if the
# node is written by hand.
#
# Platform-agnostic C11 with no Zephyr dependency, so tests/host compiles the
# same sources directly (tests/host/sources.sh).

if(CONFIG_WOZ_MATTER)

  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_library_named(woz_matter)
  zephyr_library_sources(
    ${SRC}/matter_tlv.c
    ${SRC}/matter_msg.c)

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

endif(CONFIG_WOZ_MATTER)
