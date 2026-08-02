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

# The application half. Never both in one image: the applier belongs to the
# bootloader and the receiver to the application, and each is inert without the
# other.
if(CONFIG_WOZ_DFU_RECEIVER)

  # The update-signing key is the IMAGE-signing key, so there is one secret to
  # hold and no way for the two to drift. Passed from the sysbuild command line
  # (mk/cdk.mk) because SB_CONFIG_BOOT_SIGNATURE_KEY_FILE is evaluated a level
  # up and is not visible to an image's CMake.
  # zephyr_get(), not a plain read. A -D on the west command line lands in the
  # SYSBUILD cache, and an image's CMake only sees that through this accessor --
  # which is exactly how EXTRA_ZEPHYR_MODULES reaches the bootloader
  # (zephyr/cmake/modules/zephyr_module.cmake:39). Reading the bare variable
  # finds nothing and reports a missing key that was in fact passed.
  zephyr_get(WOZ_DFU_KEY)

  if(NOT WOZ_DFU_KEY)
    message(FATAL_ERROR
      "CONFIG_WOZ_DFU_RECEIVER is set but WOZ_DFU_KEY is not.\n"
      "  The receiver verifies a staged update against the public half of the\n"
      "  image-signing key, so it cannot be built without knowing which key.\n"
      "  Pass -DWOZ_DFU_KEY=<absolute path to the signing key>.")
  endif()
  if(NOT EXISTS "${WOZ_DFU_KEY}")
    message(FATAL_ERROR "WOZ_DFU_KEY does not exist: ${WOZ_DFU_KEY}\n  Fix: make dfu-key")
  endif()

  set(woz_dfu_pubkey_c ${CMAKE_CURRENT_BINARY_DIR}/woz_dfu_pubkey.c)
  add_custom_command(
    OUTPUT ${woz_dfu_pubkey_c}
    COMMAND ${PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/emit_pubkey.py
            "${WOZ_DFU_KEY}" ${woz_dfu_pubkey_c}
    DEPENDS "${WOZ_DFU_KEY}" ${CMAKE_CURRENT_SOURCE_DIR}/scripts/emit_pubkey.py
    COMMENT "woz_dfu: extracting the update-signing public key")

  zephyr_library_named(woz_dfu)
  zephyr_library_sources(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dfu_receiver.c
    ${woz_dfu_pubkey_c})

  # zephyr_include_directories, not the _library_ form: the transport lives in
  # the application (firmware/src/dfu_ble_zephyr.c) and has to include
  # woz_dfu_rx.h. The _library_ form scopes the path to this module's own
  # library only, and the app then fails to find the header.
  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

endif()
