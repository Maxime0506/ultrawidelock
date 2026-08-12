# The UWB layer: the ranging engine, the vendored Qorvo decadriver, and this
# port's two DW3110 platform backends.
#
# Included by ports/freertos-nrf52833/CMakeLists.txt, following that file's
# layer-at-a-time rule: this one is added once the layers below it link.
#
# The source set is the role manifests in modules/{woz_uwb,woz_dw3000}/roles/,
# read through cmake/woz_roles.cmake -- the same lists and the same reader the
# Zephyr module and the ESP-IDF component use. A source assigned to a role
# belongs in its manifest and nowhere else; that is what has kept three ports
# from drifting apart, and it is why nothing is listed literally here except
# the two files this port actually owns.

get_filename_component(WOZ_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${WOZ_REPO_ROOT}/cmake/woz_roles.cmake")

set(WOZ_UWB_ROLES "${WOZ_REPO_ROOT}/modules/woz_uwb/roles")
set(WOZ_DW_ROLES "${WOZ_REPO_ROOT}/modules/woz_dw3000/roles")

# Named once, in a list of their own, because a manifest that is renamed or
# deleted expands to nothing rather than to an error: the role's sources simply
# vanish and the failure arrives at link time as an unrelated missing symbol.
# tests/ports/freertos-nrf52833/uwb_sources_check.sh asserts each one exists.
set(WOZ_UWB_ROLE_LISTS
  "${WOZ_UWB_ROLES}/base_driver.list"
  "${WOZ_UWB_ROLES}/base_engine.list"
  "${WOZ_UWB_ROLES}/responder_driver.list"
  "${WOZ_UWB_ROLES}/responder_engine.list"
  "${WOZ_UWB_ROLES}/ccc_keys.list"
  "${WOZ_UWB_ROLES}/ccc_engine.list"
  # The PSA provider, not the mbedTLS variant ports/esp32 selects. The CCC STS
  # key derivation reaches AES-ECB through a seam behind ccc_kdf.h, and this
  # port has a PSA provider where that one did not: crypto/ builds Mbed TLS
  # standalone with the PSA core on, for aliro_prim_psa.c's sake. Selecting the
  # mbedTLS variant here would link a second, lower-level path to the same
  # primitive for no reason.
  "${WOZ_UWB_ROLES}/crypto_psa.list"
  "${WOZ_UWB_ROLES}/aliro_adapter.list"
  "${WOZ_UWB_ROLES}/aliro_codec.list"
  "${WOZ_DW_ROLES}/core.list"
  "${WOZ_DW_ROLES}/chip_dw3000.list"
)

set(WOZ_UWB_SOURCES)
foreach(_woz_list IN LISTS WOZ_UWB_ROLE_LISTS)
  if(NOT EXISTS "${_woz_list}")
    message(FATAL_ERROR "UWB role manifest is missing: ${_woz_list}")
  endif()
  woz_role_sources("${_woz_list}" _woz_role_srcs)
  list(APPEND WOZ_UWB_SOURCES ${_woz_role_srcs})
endforeach()

add_library(woz_uwb STATIC
  ${WOZ_UWB_SOURCES}
  "${WOZ_PORT_DIR}/uwb/dw3000_spi_freertos.c"
  "${WOZ_PORT_DIR}/uwb/dw3000_hw_freertos.c"
  # This port's half of uwb_seam.h. The Zephyr build gets it from uwb_rxdiag.c,
  # which is a Zephyr-module literal and not in any role manifest, so each
  # non-Zephyr port supplies the essential chain itself.
  "${WOZ_PORT_DIR}/uwb/woz_seam_stubs.c"
)

target_include_directories(woz_uwb PUBLIC
  "${WOZ_REPO_ROOT}/modules/woz_port/include"
  "${WOZ_REPO_ROOT}/modules/woz_uwb/include"
  "${WOZ_REPO_ROOT}/modules/woz_dw3000/include"
  "${WOZ_PORT_DIR}/include"
)
target_include_directories(woz_uwb PRIVATE
  "${WOZ_REPO_ROOT}/modules/woz_uwb/src/driver"
  "${WOZ_REPO_ROOT}/modules/woz_uwb/src/fira"
  "${WOZ_REPO_ROOT}/modules/woz_uwb/src/ccc"
  "${WOZ_REPO_ROOT}/modules/woz_uwb/src/facade"
  "${WOZ_REPO_ROOT}/modules/woz_uwb/src/aliro"
  "${WOZ_REPO_ROOT}/modules/woz_dw3000/dwt_uwb_driver"
  "${WOZ_REPO_ROOT}/modules/woz_dw3000/dwt_uwb_driver/lib/qmath/include"
  "${WOZ_PORT_DIR}/uwb"
)

target_compile_definitions(woz_uwb PUBLIC
  # The branch every shared source takes. woz_port.h and woz_log.h are written
  # as one file per contract with a branch per platform, and this is the switch
  # that selects ours; without it the engine falls through to the host branch's
  # #error, or worse, compiles against nothing and asks the linker for Zephyr.
  WOZ_PORT_FREERTOS=1
  CONFIG_WOZ_UWB=1
  CONFIG_WOZ_UWB_RESPONDER=1
  CONFIG_WOZ_ALIRO=1
  CONFIG_DW3000=1
  CONFIG_DW3000_CHIP_DW3000=1
  CONFIG_WOZ_CRYPTO_PSA=1
  # Not optional and not a diagnostic. This is a single core and BLE shares it
  # with the ranging callbacks, so the next round's POLL and Response overwrite
  # the live DS-TWR timestamps before Final_Data is processed and the distances
  # come out kilometres wide. The nRF5340 oracle does not need it because its
  # network core is not doing this.
  CONFIG_WOZ_UWB_FINAL_SNAPSHOT=1
)

# The kernel headers, the board's platform hooks, and Mbed TLS's PSA core: the
# engine takes its OS surface from woz_port.h, its logging and timebase from the
# board, and its AES-ECB from PSA.
target_link_libraries(woz_uwb PUBLIC woz_kernel woz_board woz_mbedtls)

# ----------------------------------------------------------------------------
# Proof that this layer links, which the product image does not currently give.
#
# Nothing calls into UWB yet -- the application is still a skeleton -- so
# --gc-sections drops the whole archive and the image links whether or not this
# layer's undefined symbols could ever be satisfied. Compiling every source is
# not the same claim, and it is the weaker one: an engine that references a
# vendor function nobody supplies compiles perfectly and fails only when
# something finally reaches it.
#
# So this target forces the archive in whole and links it against the same
# libraries the product does. It is not flashed and is not part of the image;
# it exists to make the symbol set close now rather than at the far end of the
# application work.
# ----------------------------------------------------------------------------
add_executable(woz_uwb_link_check "${WOZ_PORT_DIR}/uwb/link_check.c")
target_link_libraries(woz_uwb_link_check PRIVATE
  -Wl,--whole-archive woz_uwb -Wl,--no-whole-archive
  woz_freertos_port
)
target_link_options(woz_uwb_link_check PRIVATE
  "-T${WOZ_PORT_DIR}/board/nrf52833_lock.ld"
  "-L${WOZ_PORT_DIR}/board"
  --specs=nano.specs
  --specs=nosys.specs
)
set_target_properties(woz_uwb_link_check PROPERTIES SUFFIX ".elf")
