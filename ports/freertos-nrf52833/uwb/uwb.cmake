# The UWB layer: the ranging engine, the vendored Qorvo decadriver, and this
# port's two DW3110 platform backends.
#
# Included by ports/freertos-nrf52833/CMakeLists.txt, following that file's
# layer-at-a-time rule: this one is added once the layers below it link.
#
# The source set is the role manifests in modules/{ultrawidelock_uwb,ultrawidelock_dw3000}/roles/,
# read through cmake/woz_roles.cmake -- the same lists and the same reader the
# Zephyr module and the ESP-IDF component use. A source assigned to a role
# belongs in its manifest and nowhere else; that is what has kept three ports
# from drifting apart, and it is why nothing is listed literally here except
# the two files this port actually owns.

get_filename_component(WOZ_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${WOZ_REPO_ROOT}/cmake/woz_roles.cmake")

set(ULTRAWIDELOCK_UWB_ROLES "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/roles")
set(WOZ_DW_ROLES "${WOZ_REPO_ROOT}/modules/ultrawidelock_dw3000/roles")

# Named once, in a list of their own, because a manifest that is renamed or
# deleted expands to nothing rather than to an error: the role's sources simply
# vanish and the failure arrives at link time as an unrelated missing symbol.
# tests/ports/freertos-nrf52833/uwb_sources_check.sh asserts each one exists.
set(ULTRAWIDELOCK_UWB_ROLE_LISTS
  "${ULTRAWIDELOCK_UWB_ROLES}/base_driver.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/base_engine.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/responder_driver.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/responder_engine.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/ccc_keys.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/ccc_engine.list"
  # The PSA provider, not the mbedTLS variant ports/esp32 selects. The CCC STS
  # key derivation reaches AES-ECB through a seam behind ccc_kdf.h, and this
  # port has a PSA provider where that one did not: crypto/ builds Mbed TLS
  # standalone with the PSA core on, for aliro_prim_psa.c's sake. Selecting the
  # mbedTLS variant here would link a second, lower-level path to the same
  # primitive for no reason.
  "${ULTRAWIDELOCK_UWB_ROLES}/crypto_psa.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/ultrawidelock_adapter.list"
  "${ULTRAWIDELOCK_UWB_ROLES}/ultrawidelock_codec.list"
  "${WOZ_DW_ROLES}/core.list"
  "${WOZ_DW_ROLES}/chip_dw3000.list"
)

set(ULTRAWIDELOCK_UWB_SOURCES)
foreach(_woz_list IN LISTS ULTRAWIDELOCK_UWB_ROLE_LISTS)
  if(NOT EXISTS "${_woz_list}")
    message(FATAL_ERROR "UWB role manifest is missing: ${_woz_list}")
  endif()
  woz_role_sources("${_woz_list}" _woz_role_srcs)
  list(APPEND ULTRAWIDELOCK_UWB_SOURCES ${_woz_role_srcs})
endforeach()

add_library(ultrawidelock_uwb STATIC
  ${ULTRAWIDELOCK_UWB_SOURCES}
  "${WOZ_PORT_DIR}/uwb/dw3000_spi_freertos.c"
  "${WOZ_PORT_DIR}/uwb/dw3000_hw_freertos.c"
  # This port's half of uwb_seam.h. The Zephyr build gets it from uwb_rxdiag.c,
  # which is a Zephyr-module literal and not in any role manifest, so each
  # non-Zephyr port supplies the essential chain itself.
  "${WOZ_PORT_DIR}/uwb/ultrawidelock_seam_stubs.c"
  # The port's bring-up, and the only thing in the image that calls the layer
  # until the Aliro seam is wired.
  "${WOZ_PORT_DIR}/uwb/woz_freertos_uwb.c"
)

target_include_directories(ultrawidelock_uwb PUBLIC
  "${WOZ_REPO_ROOT}/modules/woz_port/include"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/include"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_dw3000/include"
  "${WOZ_PORT_DIR}/include"
)
target_include_directories(ultrawidelock_uwb PRIVATE
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/src/driver"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/src/fira"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/src/ccc"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/src/facade"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_uwb/src/cred"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_dw3000/dwt_uwb_driver"
  "${WOZ_REPO_ROOT}/modules/ultrawidelock_dw3000/dwt_uwb_driver/lib/qmath/include"
  "${WOZ_PORT_DIR}/uwb"
)

target_compile_definitions(ultrawidelock_uwb PUBLIC
  # The branch every shared source takes. woz_port.h and woz_log.h are written
  # as one file per contract with a branch per platform, and this is the switch
  # that selects ours; without it the engine falls through to the host branch's
  # #error, or worse, compiles against nothing and asks the linker for Zephyr.
  WOZ_PORT_FREERTOS=1
  CONFIG_ULTRAWIDELOCK_UWB=1
  CONFIG_ULTRAWIDELOCK_UWB_RESPONDER=1
  CONFIG_WOZ_ALIRO=1
  CONFIG_DW3000=1
  CONFIG_DW3000_CHIP_DW3000=1
  CONFIG_WOZ_CRYPTO_PSA=1
  # Not optional and not a diagnostic. This is a single core and BLE shares it
  # with the ranging callbacks, so the next round's POLL and Response overwrite
  # the live DS-TWR timestamps before Final_Data is processed and the distances
  # come out kilometres wide. The nRF5340 oracle does not need it because its
  # network core is not doing this.
  CONFIG_ULTRAWIDELOCK_UWB_FINAL_SNAPSHOT=1
)

# The kernel headers, the board's platform hooks, and Mbed TLS's PSA core: the
# engine takes its OS surface from woz_port.h, its logging and timebase from the
# board, and its AES-ECB from PSA.
target_link_libraries(ultrawidelock_uwb PUBLIC woz_kernel woz_board woz_mbedtls)

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
add_executable(ultrawidelock_uwb_link_check "${WOZ_PORT_DIR}/uwb/link_check.c")
target_link_libraries(ultrawidelock_uwb_link_check PRIVATE
  -Wl,--whole-archive ultrawidelock_uwb -Wl,--no-whole-archive
  woz_freertos_port
)
target_link_options(ultrawidelock_uwb_link_check PRIVATE
  "-T${WOZ_PORT_DIR}/board/nrf52833_lock.ld"
  "-L${WOZ_PORT_DIR}/board"
  --specs=nano.specs
  --specs=nosys.specs
)
set_target_properties(ultrawidelock_uwb_link_check PROPERTIES SUFFIX ".elf")

# ----------------------------------------------------------------------------
# What the layer costs when it is reached, rather than when it is forced in.
#
# The link check above is the right shape for proving symbol closure and the
# wrong one for answering size: --whole-archive keeps everything, so it reports
# 280,628 bytes, roughly six times what the layer actually costs. That is an
# upper bound, and a bound loose enough to plan the wrong things around.
#
# --gc-sections keeps what is referenced, so the honest number comes from naming
# the entry points an application really calls and letting the linker walk out
# from them. Three links, because the useful figures are differences:
#
#   baseline   no UWB roots -- the floor the other two are measured against
#   facade     the responder surface a lock calls
#   responder  that plus the Aliro ranging-setup seam, which is the real build
#
# The gap between the last two is what a range-only responder saves and an
# Aliro one cannot.
#
# This is in the build graph rather than in someone's notes because a number
# that regresses silently is worth about what a link check that never runs is
# worth, and this port has already paid that once: freertos-ncs-source-check
# stopped compiling for weeks because nothing ran it.
#
# The figure is relative to these roots. A build that also reaches the
# diagnostics, cirdiag or the flight recorder costs more, and none of that
# appears here.
# ----------------------------------------------------------------------------

# The app-facing responder surface: lifecycle plus the range accessors a lock
# reads.
set(ULTRAWIDELOCK_UWB_REACH_FACADE
  ultrawidelock_uwb_start_aliro ultrawidelock_uwb_stop ultrawidelock_uwb_prewarm
  ultrawidelock_uwb_set_range_listener ultrawidelock_uwb_trusted_range_cm
  ultrawidelock_uwb_trusted_range_after_checked_cm ultrawidelock_uwb_last_range_cm
  ultrawidelock_uwb_range_generation
)

# The Aliro ranging-setup seam modules/woz_aliro/src/aliro_ranging.c drives. A
# responder that completes M1-M4 needs these, and the facade alone does not pull
# them in, so measuring without them understates a real Aliro build.
set(ULTRAWIDELOCK_UWB_REACH_ALIRO
  cherry_create cherry_destroy_sync ultrawidelock_uwb_adapter_create_reader
  ultrawidelock_uwb_session_create ultrawidelock_uwb_session_set_ursk
  ultrawidelock_uwb_session_set_protocol_version ultrawidelock_uwb_session_message_handle
  ultrawidelock_uwb_session_destroy ultrawidelock_uwb_session_message_free
  ultrawidelock_uwb_session_event_free
)

# The interrupt entry the vector table references on a real target. Without it
# the worker's whole call tree is unreachable and the measurement is a fiction.
set(ULTRAWIDELOCK_UWB_REACH_IRQ woz_freertos_dw3000_irq_handler)

# <name> <roots...> -- one variant of the same link, differing only in roots.
function(ultrawidelock_uwb_add_reach_variant _name)
  add_executable("${_name}" "${WOZ_PORT_DIR}/uwb/link_check.c")
  # Not --whole-archive: the point is to let the collector do its work.
  target_link_libraries("${_name}" PRIVATE ultrawidelock_uwb woz_freertos_port)
  set(_undef "")
  foreach(_sym ${ARGN})
    list(APPEND _undef "-Wl,--undefined=${_sym}")
  endforeach()
  target_link_options("${_name}" PRIVATE
    "-T${WOZ_PORT_DIR}/board/nrf52833_lock.ld"
    "-L${WOZ_PORT_DIR}/board"
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    "-Wl,-Map=$<TARGET_FILE_DIR:${_name}>/${_name}.map"
    ${_undef}
  )
  set_target_properties("${_name}" PROPERTIES SUFFIX ".elf" EXCLUDE_FROM_ALL TRUE)
endfunction()

ultrawidelock_uwb_add_reach_variant(ultrawidelock_uwb_reach_baseline)
ultrawidelock_uwb_add_reach_variant(ultrawidelock_uwb_reach_facade
  ${ULTRAWIDELOCK_UWB_REACH_IRQ} ${ULTRAWIDELOCK_UWB_REACH_FACADE})
ultrawidelock_uwb_add_reach_variant(ultrawidelock_uwb_reach_responder
  ${ULTRAWIDELOCK_UWB_REACH_IRQ} ${ULTRAWIDELOCK_UWB_REACH_FACADE} ${ULTRAWIDELOCK_UWB_REACH_ALIRO})

add_custom_target(ultrawidelock_uwb_reach
  COMMAND "${CMAKE_COMMAND}" -E env bash
          "${WOZ_PORT_DIR}/uwb/reach_report.sh"
          "${CMAKE_SIZE}"
          "$<TARGET_FILE:ultrawidelock_uwb_reach_baseline>"
          "$<TARGET_FILE:ultrawidelock_uwb_reach_facade>"
          "$<TARGET_FILE:ultrawidelock_uwb_reach_responder>"
          "$<TARGET_FILE_DIR:ultrawidelock_uwb_reach_responder>/ultrawidelock_uwb_reach_responder.map"
  DEPENDS ultrawidelock_uwb_reach_baseline ultrawidelock_uwb_reach_facade ultrawidelock_uwb_reach_responder
  COMMENT "Measuring the UWB layer's reachable set"
  VERBATIM
)
