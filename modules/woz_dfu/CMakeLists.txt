# Delta update, bootloader half. Contributes nothing unless
# CONFIG_WOZ_DFU_APPLIER=y, so every image that does not ask for it -- which is
# every image except MCUboot on the DWM3001CDK -- stays byte-identical.
#
# This module is on the module path of ALL images, not just the application.
# That is how a Zephyr module reaches the bootloader at all: sysbuild builds
# MCUboot as an ordinary Zephyr application, so it loads the same modules, and
# NCS injects its own MCUboot hooks exactly this way
# (nrf/modules/mcuboot/hooks/CMakeLists.txt gates on CONFIG_BOOT_IMAGE_ACCESS_HOOKS).
# The Kconfig gate above is therefore the only thing keeping this out of the
# application image.

if(CONFIG_WOZ_DFU_APPLIER)

  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  set(DETOOLS ${REPO_ROOT}/deps/detools/c)

  zephyr_library_named(woz_dfu)
  zephyr_library_sources(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dfu_applier.c
    ${DETOOLS}/detools.c
    ${DETOOLS}/heatshrink/heatshrink_decoder.c)

  zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DETOOLS}
    ${DETOOLS}/heatshrink)

  # LZMA is the expensive decompressor and nothing here creates lzma patches;
  # dropping it is most of why this fits in the bootloader at all. `none` is
  # kept because it costs almost nothing and makes an uncompressed patch
  # debuggable on the bench. FILE_IO would pull in stdio.
  zephyr_library_compile_definitions(
    DETOOLS_CONFIG_FILE_IO=0
    DETOOLS_CONFIG_COMPRESSION_LZMA=0
    DETOOLS_CONFIG_COMPRESSION_CRLE=0
    DETOOLS_CONFIG_COMPRESSION_NONE=1
    DETOOLS_CONFIG_COMPRESSION_HEATSHRINK=1)

  # Vendored third-party C, not held to this tree's warning level.
  set_source_files_properties(
    ${DETOOLS}/detools.c
    ${DETOOLS}/heatshrink/heatshrink_decoder.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare")

endif()
