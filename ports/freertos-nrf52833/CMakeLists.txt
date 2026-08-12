# The nRF52833 FreeRTOS target build.
#
# Three vendor trees supply this image and none of them is copied into the
# repository, so each is located by a cache variable and pinned by
# platform.lock.yml:
#
#   WOZ_QORVO_SDK_DIR  the extracted DW3/QM33 SDK 1.1.1 -- FreeRTOS kernel,
#                      board support, and the DW3000 driver
#   WOZ_NCS_WORKSPACE  the west workspace -- the hal_nordic nrfx and MDK the
#                      radio stack is built against, CMSIS, nrfxlib, and the
#                      pinned NCS sources
#   WOZ_NIMBLE_DIR     the mynewt-nimble checkout -- the BLE host
#
# The build is assembled in layers and each layer is only added once the one
# below it links, because a first link that reports its section sizes is worth
# more here than a complete graph that does not.

cmake_minimum_required(VERSION 3.20)

project(woz_freertos_nrf52833 C ASM)

# Cached rather than a plain variable so woz_freertos_image() still resolves it
# when the application calls it from the parent directory scope.
set(WOZ_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "FreeRTOS nRF52833 port directory")

foreach(_woz_dir WOZ_QORVO_SDK_DIR WOZ_NCS_WORKSPACE)
  if(NOT ${_woz_dir})
    message(FATAL_ERROR "${_woz_dir} is not set; see ports/freertos-nrf52833/README.md")
  endif()
  if(NOT IS_DIRECTORY "${${_woz_dir}}")
    message(FATAL_ERROR "${_woz_dir} is not a directory: ${${_woz_dir}}")
  endif()
endforeach()

set(WOZ_NRF5_SDK "${WOZ_QORVO_SDK_DIR}/SDK_BSP/Nordic/SDK_17_1_0")
set(WOZ_FREERTOS_SRC "${WOZ_NRF5_SDK}/external/freertos")
set(WOZ_NRFX "${WOZ_NCS_WORKSPACE}/modules/hal/nordic/nrfx")
set(WOZ_BSP "${WOZ_NRFX}/bsp/stable")
set(WOZ_MDK "${WOZ_BSP}/mdk")
set(WOZ_MODULES_DIR "${CMAKE_CURRENT_LIST_DIR}/../../modules")
set(WOZ_CMSIS "${WOZ_NCS_WORKSPACE}/modules/hal/cmsis/CMSIS/Core/Include")

foreach(_woz_path "${WOZ_FREERTOS_SRC}/source/tasks.c" "${WOZ_MDK}/nrf52833.h"
                  "${WOZ_CMSIS}/core_cm4.h")
  if(NOT EXISTS "${_woz_path}")
    message(FATAL_ERROR "expected vendor file is missing: ${_woz_path}")
  endif()
endforeach()

# ----------------------------------------------------------------------------
# Device layer: CMSIS, the pinned MDK, and the nrfx hal.
#
# This is the hal_nordic tree the radio stack is built against, deliberately
# not the older nrfx bundled with the Qorvo SDK. Two register maps in one image
# is a class of bug that does not announce itself, and the radio's requirement
# is the harder one, so it wins.
# ----------------------------------------------------------------------------
add_library(woz_device STATIC
  "${WOZ_MDK}/system_nrf52833.c"
)
target_include_directories(woz_device PUBLIC
  "${WOZ_CMSIS}"
  "${WOZ_MDK}"
  "${WOZ_NRFX}"
  "${WOZ_NRFX}/hal"
  "${WOZ_NRFX}/drivers"
  "${WOZ_NRFX}/drivers/include"
  "${WOZ_NRFX}/bsp"
  "${WOZ_NRFX}/bsp/stable"
  "${WOZ_NRFX}/bsp/stable/templates"
  "${WOZ_PORT_DIR}/board/nrfx_config"
)
target_compile_definitions(woz_device PUBLIC
  NRF52833_XXAA
  __HEAP_SIZE=0
  __STACK_SIZE=0
)

# ----------------------------------------------------------------------------
# Kernel layer: FreeRTOS V10.0.0 and its Cortex-M4F port, both unmodified.
#
# port_cmsis_systick.c is deliberately absent. It drives RTC1 through
# nrf_drv_clock, which fights MPSL for the low-frequency clock, and it prescales
# the counter board/time_freertos.c reads at full rate.
# board/tick_freertos.c replaces it and supplies the same two entry points.
# ----------------------------------------------------------------------------
add_library(woz_kernel STATIC
  "${WOZ_FREERTOS_SRC}/source/croutine.c"
  "${WOZ_FREERTOS_SRC}/source/event_groups.c"
  "${WOZ_FREERTOS_SRC}/source/list.c"
  "${WOZ_FREERTOS_SRC}/source/queue.c"
  "${WOZ_FREERTOS_SRC}/source/stream_buffer.c"
  "${WOZ_FREERTOS_SRC}/source/tasks.c"
  "${WOZ_FREERTOS_SRC}/source/timers.c"
  "${WOZ_FREERTOS_SRC}/source/portable/MemMang/heap_4.c"
  "${WOZ_FREERTOS_SRC}/portable/GCC/nrf52/port.c"
  "${WOZ_FREERTOS_SRC}/portable/CMSIS/nrf52/port_cmsis.c"
)
target_include_directories(woz_kernel PUBLIC
  "${WOZ_FREERTOS_SRC}/source/include"
  "${WOZ_FREERTOS_SRC}/portable/GCC/nrf52"
  "${WOZ_FREERTOS_SRC}/portable/CMSIS/nrf52"
  "${WOZ_PORT_DIR}/board"
  "${WOZ_PORT_DIR}/kernel/kernel_compat"
)
target_link_libraries(woz_kernel PUBLIC woz_device)

# heap_4 coalesces adjacent free blocks, which matters because Mbed TLS
# allocates and frees bignums of varying size for the duration of the product.
# heap_1, which the Qorvo baseline is closest to using, cannot free at all.

# The vendor kernel predates several warnings this repository builds with, and
# it is compiled unmodified on purpose, so they are relaxed for it alone.
target_compile_options(woz_kernel PRIVATE
  -Wno-unused-parameter
  -Wno-sign-compare
)

# ----------------------------------------------------------------------------
# Board layer: the parts of the port that touch the chip directly.
#
# Only the files whose dependencies are already in the graph are listed. The
# rest of the board -- entropy, flash, temperature, logging -- arrives with the
# radio layer, because they call MPSL.
# ----------------------------------------------------------------------------
add_library(woz_board STATIC
  "${WOZ_PORT_DIR}/board/startup_freertos.c"
  "${WOZ_PORT_DIR}/board/kernel_hooks_freertos.c"
  "${WOZ_PORT_DIR}/board/tick_freertos.c"
  "${WOZ_PORT_DIR}/board/time_freertos.c"
  "${WOZ_PORT_DIR}/board/fault_freertos.c"
  "${WOZ_PORT_DIR}/board/log_rtt_freertos.c"
)
target_include_directories(woz_board PUBLIC
  "${WOZ_PORT_DIR}/include"
)
target_link_libraries(woz_board PUBLIC woz_kernel)

# A real cycle, declared rather than worked around. The kernel calls back into
# the board for configASSERT, the stack-overflow hook, and the static memory the
# idle and timer tasks are built from; the board calls into the kernel for
# everything else. CMake resolves a static-library cycle by repeating the
# archives on the link line, which is what this needs.
target_link_libraries(woz_kernel PRIVATE woz_board)

# ----------------------------------------------------------------------------
# Crypto layer: Mbed TLS 3.6.6, standalone with the PSA core on.
#
# Built from the library sources directly rather than through upstream's CMake,
# because upstream's project builds three libraries, a test framework, and its
# own programs, and this image wants one of the three with a configuration this
# port supplies wholesale.
#
# Only libmbedcrypto's sources are here. TLS and X.509 are not compiled at all:
# nothing in this product speaks either, and the whole reason OpenThread was
# left on its upstream OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS default rather than
# moved to PSA was to keep the surface this small.
#
# The configuration is forced in rather than selected by macro, so there is one
# answer to what this library was built with and it is a file in this tree.
# ----------------------------------------------------------------------------
set(WOZ_MBEDTLS_DIR "${WOZ_NCS_WORKSPACE}/modules/crypto/mbedtls")
if(NOT EXISTS "${WOZ_MBEDTLS_DIR}/library/psa_crypto.c")
  message(FATAL_ERROR "pinned Mbed TLS is missing: ${WOZ_MBEDTLS_DIR}")
endif()

file(GLOB WOZ_MBEDTLS_SOURCES "${WOZ_MBEDTLS_DIR}/library/*.c")
list(FILTER WOZ_MBEDTLS_SOURCES EXCLUDE REGEX "/(ssl_|x509|net_sockets|debug|mps_)")

add_library(woz_mbedtls STATIC
  ${WOZ_MBEDTLS_SOURCES}
  "${WOZ_PORT_DIR}/crypto/mbedtls_threading_freertos.c"
  "${WOZ_PORT_DIR}/crypto/mbedtls_platform_freertos.c"
  "${WOZ_PORT_DIR}/crypto/crypto_init_freertos.c"
)
target_include_directories(woz_mbedtls PUBLIC
  "${WOZ_MBEDTLS_DIR}/include"
  "${WOZ_PORT_DIR}/crypto"
)
# The NCS fork keeps the library-internal headers under include/library rather
# than beside the sources, so both are on the private path.
target_include_directories(woz_mbedtls PRIVATE
  "${WOZ_MBEDTLS_DIR}/library"
  "${WOZ_MBEDTLS_DIR}/include/library"
)
target_compile_definitions(woz_mbedtls PUBLIC
  MBEDTLS_CONFIG_FILE="mbedtls_config_freertos.h"
  MBEDTLS_PSA_CRYPTO_CONFIG_FILE="psa_crypto_config_freertos.h"
)
target_link_libraries(woz_mbedtls PUBLIC woz_board)

# ----------------------------------------------------------------------------
# BLE host layer: Apache NimBLE.
#
# Built from the upstream checkout with the port's own syscfg, which is why
# ble/nimble_syscfg comes first on the include path: upstream ships its own
# syscfg/syscfg.h under porting/nimble/include and the first one found wins.
# The port's selection is peripheral-only -- no central, no observer, no
# extended advertising, one connection, one credit-based L2CAP channel.
#
# The host and its porting layer are taken by directory rather than by file.
# That is the upstream build's own granularity: every porting example lists
# these directories wholesale, and picking files by hand would mean tracking
# upstream's internal factoring at every re-pin for no reduction in image size,
# since --gc-sections already drops what the configuration does not reach.
# ----------------------------------------------------------------------------
if(NOT WOZ_NIMBLE_DIR)
  message(FATAL_ERROR "WOZ_NIMBLE_DIR is not set; see ports/freertos-nrf52833/README.md")
endif()
if(NOT EXISTS "${WOZ_NIMBLE_DIR}/porting/npl/freertos/src/npl_os_freertos.c")
  message(FATAL_ERROR "WOZ_NIMBLE_DIR is not a mynewt-nimble checkout: ${WOZ_NIMBLE_DIR}")
endif()

file(GLOB WOZ_NIMBLE_SOURCES
  "${WOZ_NIMBLE_DIR}/porting/nimble/src/*.c"
  "${WOZ_NIMBLE_DIR}/porting/npl/freertos/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/host/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/host/util/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/host/services/gap/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/host/services/gatt/src/*.c"
  "${WOZ_NIMBLE_DIR}/nimble/transport/src/*.c"
)

# ble_gatts_lcl.c dumps the local GATT database over Mynewt's console package,
# which this build does not have and does not want: the port's log sink is
# SEGGER RTT. Upstream's own porting examples leave it out for the same reason.
list(REMOVE_ITEM WOZ_NIMBLE_SOURCES "${WOZ_NIMBLE_DIR}/nimble/host/src/ble_gatts_lcl.c")

# hal_timer.c and the cputime sources are the link layer's timebase. The
# controller here is Nordic's, reached over HCI, so nothing in this image drives
# them and their only effect would be to claim a hardware timer that
# peripherals.yml has already given to someone else.
list(REMOVE_ITEM WOZ_NIMBLE_SOURCES
  "${WOZ_NIMBLE_DIR}/porting/nimble/src/hal_timer.c"
  "${WOZ_NIMBLE_DIR}/porting/nimble/src/os_cputime.c"
  "${WOZ_NIMBLE_DIR}/porting/nimble/src/os_cputime_pwr2.c")

add_library(woz_nimble STATIC ${WOZ_NIMBLE_SOURCES})
target_include_directories(woz_nimble PUBLIC
  "${WOZ_PORT_DIR}/ble/nimble_syscfg"
  "${WOZ_NIMBLE_DIR}/porting/nimble/include"
  "${WOZ_NIMBLE_DIR}/porting/npl/freertos/include"
  "${WOZ_NIMBLE_DIR}/nimble/include"
  "${WOZ_NIMBLE_DIR}/nimble/host/include"
  "${WOZ_NIMBLE_DIR}/nimble/host/util/include"
  "${WOZ_NIMBLE_DIR}/nimble/host/services/gap/include"
  "${WOZ_NIMBLE_DIR}/nimble/host/services/gatt/include"
  "${WOZ_NIMBLE_DIR}/nimble/transport/include"
)
target_link_libraries(woz_nimble PUBLIC woz_kernel woz_mbedtls)

# Upstream is compiled unmodified and predates some of the warnings this
# repository builds with. -Wundef in particular cannot survive contact with
# NimBLE's headers: its configuration scheme tests MYNEWT_VAL_* and
# NIMBLE_CFG_* names that are meant to evaluate to zero when unset, so the
# warning fires on the mechanism rather than on a mistake. It is relaxed on the
# INTERFACE side as well, because the port's own files that include those
# headers hit the same thing, and it would be worse to drop the warning for the
# whole port than to scope it to the files that talk to NimBLE.
target_compile_options(woz_nimble
  PRIVATE -Wno-sign-compare
  PUBLIC -Wno-undef -Wno-unused-parameter)

# ----------------------------------------------------------------------------
# Radio layer: MPSL and the SoftDevice Controller.
#
# Both are supplied as binaries built for this part and this float ABI, which is
# the reason the toolchain file makes hard float non-negotiable. The peripheral
# variant of the controller is linked, not multirole: this product advertises
# and accepts one connection and never initiates one, and the central and
# multirole archives carry link-layer code it would never execute.
#
# The board files that arrive with this layer do so because they call MPSL:
# temperature reads through mpsl_temperature_get, since MPSL owns TEMP and
# calibrates the low-frequency clock with it.
# ----------------------------------------------------------------------------
set(WOZ_NRFXLIB "${WOZ_NCS_WORKSPACE}/nrfxlib")
set(WOZ_MPSL_LIB "${WOZ_NRFXLIB}/mpsl/lib/nrf52/hard-float/libmpsl.a")
set(WOZ_SDC_LIB
  "${WOZ_NRFXLIB}/softdevice_controller/lib/nrf52/hard-float/libsoftdevice_controller_peripheral.a")

# MPSL calls a front-end module API unconditionally, so something has to answer
# it even though the DWM3001CDK has no front-end module: the module is a bare
# nRF52833 driving the DW3110, with no PA or LNA in the radio path. The common
# archive is that answer -- it is the dispatch layer, and linking it without any
# of the device-specific FEM archives is how "no FEM" is expressed.
set(WOZ_FEM_LIB "${WOZ_NRFXLIB}/mpsl/fem/common/lib/nrf52/hard-float/libmpsl_fem_common.a")

foreach(_woz_lib "${WOZ_MPSL_LIB}" "${WOZ_SDC_LIB}" "${WOZ_FEM_LIB}")
  if(NOT EXISTS "${_woz_lib}")
    message(FATAL_ERROR "pinned radio binary is missing: ${_woz_lib}")
  endif()
endforeach()

add_library(woz_radio STATIC
  "${WOZ_PORT_DIR}/radio/radio_start_freertos.c"
  "${WOZ_PORT_DIR}/radio/mpsl_freertos.c"
  "${WOZ_PORT_DIR}/board/entropy_freertos.c"
  "${WOZ_PORT_DIR}/board/temperature_freertos.c"
  # The pinned NCS opcode dispatcher, compiled unmodified against ble/hci_compat
  # exactly as make freertos-ncs-source-check compiles it, plus the port half
  # that adapts it. The controller's command API is opcode-specific, so an HCI
  # command has to be decoded before it reaches the controller.
  "${WOZ_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller/hci_internal.c"
  "${WOZ_PORT_DIR}/ble/hci_dispatcher_freertos.c"
  "${WOZ_PORT_DIR}/ble/nimble_sdc_transport.c"
  "${WOZ_PORT_DIR}/ble/nimble_host_freertos.c"
)
target_include_directories(woz_radio PUBLIC
  "${WOZ_NRFXLIB}/mpsl/include"
  "${WOZ_NRFXLIB}/mpsl/fem/include"
  "${WOZ_NRFXLIB}/softdevice_controller/include"
  "${WOZ_PORT_DIR}/ble/hci_compat"
  "${WOZ_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller"
)
target_link_libraries(woz_radio PUBLIC woz_board woz_nimble "${WOZ_MPSL_LIB}" "${WOZ_SDC_LIB}"
  "${WOZ_FEM_LIB}")

# The dispatcher reads its configuration from a forced include, the same way the
# source-check script compiles it, so the vendor file needs no edit.
set_source_files_properties(
  "${WOZ_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller/hci_internal.c"
  PROPERTIES COMPILE_OPTIONS
  "-include;${WOZ_PORT_DIR}/ble/hci_compat/woz_freertos_hci_config.h")

# The second declared cycle, and the same reasoning as the kernel's. The vector
# table in board/startup_freertos.c is the only thing that references the radio
# entry points, so without this the archive is scanned before those references
# exist and radio_start_freertos.c is never pulled in.
target_link_libraries(woz_board PUBLIC woz_radio)

# ----------------------------------------------------------------------------
# Storage layer: the persistent key-value store and its two readers.
#
# board/flash_freertos.c belongs here rather than with the rest of the board,
# because it arbitrates NVMC against the radio with MPSL timeslots and so
# cannot be linked without MPSL.
# ----------------------------------------------------------------------------
add_library(woz_storage STATIC
  "${WOZ_PORT_DIR}/board/flash_freertos.c"
  "${WOZ_PORT_DIR}/storage/kv_flash_freertos.c"
  "${WOZ_PORT_DIR}/storage/aliro_prov_kv.c"
  # The portable half of the provisioning record, from the shared module. The
  # port supplies only where the bytes live; the format, the default identity,
  # and the trust check are the same code the Zephyr and ESP32 targets run,
  # which is the entire point of the module boundary.
  "${WOZ_MODULES_DIR}/woz_aliro/src/aliro_prov.c"
)
# The provisioning backend implements a contract owned by the shared Aliro
# module, not by this port, so the module's public headers are on the path.
target_include_directories(woz_storage PUBLIC
  "${WOZ_MODULES_DIR}/woz_aliro/include"
)
target_link_libraries(woz_storage PUBLIC woz_radio)

add_library(woz_freertos_port INTERFACE)
target_link_libraries(woz_freertos_port INTERFACE woz_storage)

# ----------------------------------------------------------------------------
# Warnings and code generation, applied to the port's own code only.
# ----------------------------------------------------------------------------
foreach(_woz_target woz_board woz_radio woz_storage)
  target_compile_options(${_woz_target} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wundef
  )
endforeach()

foreach(_woz_target woz_device woz_kernel woz_board woz_radio woz_nimble woz_mbedtls woz_storage)
  target_compile_options(${_woz_target} PRIVATE
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fno-strict-aliasing
  )
endforeach()

# ----------------------------------------------------------------------------
# A helper the application uses to turn a linked ELF into something flashable
# and to print what it cost.
# ----------------------------------------------------------------------------
function(woz_freertos_image target)
  target_link_options(${target} PRIVATE
    "-T${WOZ_PORT_DIR}/board/nrf52833_lock.ld"
    "-L${WOZ_PORT_DIR}/board"
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map"
  )
  set_target_properties(${target} PROPERTIES
    LINK_DEPENDS "${WOZ_PORT_DIR}/board/nrf52833_lock.ld"
    SUFFIX ".elf"
  )
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${target}>"
            "$<TARGET_FILE_DIR:${target}>/${target}.hex"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>"
    VERBATIM
  )
endfunction()
