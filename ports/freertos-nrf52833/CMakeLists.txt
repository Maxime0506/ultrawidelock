# The nRF52833 FreeRTOS target build.
#
# Three vendor trees supply this image and none of them is copied into the
# repository, so each is located by a cache variable and pinned by
# platform.lock.yml:
#
#   ULTRAWIDELOCK_QORVO_SDK_DIR  the extracted DW3/QM33 SDK 1.1.1 -- FreeRTOS kernel,
#                      board support, and the DW3000 driver
#   ULTRAWIDELOCK_NCS_WORKSPACE  the west workspace -- the hal_nordic nrfx and MDK the
#                      radio stack is built against, CMSIS, nrfxlib, and the
#                      pinned NCS sources
#   ULTRAWIDELOCK_NIMBLE_DIR     the mynewt-nimble checkout -- the BLE host
#
# The build is assembled in layers and each layer is only added once the one
# below it links, because a first link that reports its section sizes is worth
# more here than a complete graph that does not.

cmake_minimum_required(VERSION 3.20)

project(ultrawidelock_freertos_nrf52833 C ASM)

# Cached rather than a plain variable so ultrawidelock_freertos_image() still resolves it
# when the application calls it from the parent directory scope.
set(ULTRAWIDELOCK_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "FreeRTOS nRF52833 port directory")

foreach(_ultrawidelock_dir ULTRAWIDELOCK_QORVO_SDK_DIR ULTRAWIDELOCK_NCS_WORKSPACE)
  if(NOT ${_ultrawidelock_dir})
    message(FATAL_ERROR "${_ultrawidelock_dir} is not set; see ports/freertos-nrf52833/README.md")
  endif()
  if(NOT IS_DIRECTORY "${${_ultrawidelock_dir}}")
    message(FATAL_ERROR "${_ultrawidelock_dir} is not a directory: ${${_ultrawidelock_dir}}")
  endif()
endforeach()

set(ULTRAWIDELOCK_NRF5_SDK "${ULTRAWIDELOCK_QORVO_SDK_DIR}/SDK_BSP/Nordic/SDK_17_1_0")
set(ULTRAWIDELOCK_FREERTOS_SRC "${ULTRAWIDELOCK_NRF5_SDK}/external/freertos")
set(ULTRAWIDELOCK_NRFX "${ULTRAWIDELOCK_NCS_WORKSPACE}/modules/hal/nordic/nrfx")
set(ULTRAWIDELOCK_BSP "${ULTRAWIDELOCK_NRFX}/bsp/stable")
set(ULTRAWIDELOCK_MDK "${ULTRAWIDELOCK_BSP}/mdk")
get_filename_component(ULTRAWIDELOCK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ULTRAWIDELOCK_MODULES_DIR "${ULTRAWIDELOCK_REPO_ROOT}/modules")
set(ULTRAWIDELOCK_CMSIS "${ULTRAWIDELOCK_NCS_WORKSPACE}/modules/hal/cmsis/CMSIS/Core/Include")

foreach(_ultrawidelock_path "${ULTRAWIDELOCK_FREERTOS_SRC}/source/tasks.c" "${ULTRAWIDELOCK_MDK}/nrf52833.h"
                  "${ULTRAWIDELOCK_CMSIS}/core_cm4.h")
  if(NOT EXISTS "${_ultrawidelock_path}")
    message(FATAL_ERROR "expected vendor file is missing: ${_ultrawidelock_path}")
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
add_library(ultrawidelock_soc STATIC
  "${ULTRAWIDELOCK_MDK}/system_nrf52833.c"
)
target_include_directories(ultrawidelock_soc PUBLIC
  "${ULTRAWIDELOCK_CMSIS}"
  "${ULTRAWIDELOCK_MDK}"
  "${ULTRAWIDELOCK_NRFX}"
  "${ULTRAWIDELOCK_NRFX}/hal"
  "${ULTRAWIDELOCK_NRFX}/drivers"
  "${ULTRAWIDELOCK_NRFX}/drivers/include"
  "${ULTRAWIDELOCK_NRFX}/bsp"
  "${ULTRAWIDELOCK_NRFX}/bsp/stable"
  "${ULTRAWIDELOCK_NRFX}/bsp/stable/templates"
  "${ULTRAWIDELOCK_PORT_DIR}/board/nrfx_config"
)
target_compile_definitions(ultrawidelock_soc PUBLIC
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
add_library(ultrawidelock_kernel STATIC
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/croutine.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/event_groups.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/list.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/queue.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/stream_buffer.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/tasks.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/timers.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/portable/MemMang/heap_4.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/portable/GCC/nrf52/port.c"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/portable/CMSIS/nrf52/port_cmsis.c"
)
target_include_directories(ultrawidelock_kernel PUBLIC
  "${ULTRAWIDELOCK_FREERTOS_SRC}/source/include"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/portable/GCC/nrf52"
  "${ULTRAWIDELOCK_FREERTOS_SRC}/portable/CMSIS/nrf52"
  "${ULTRAWIDELOCK_PORT_DIR}/board"
  "${ULTRAWIDELOCK_PORT_DIR}/kernel/kernel_compat"
)
target_link_libraries(ultrawidelock_kernel PUBLIC ultrawidelock_soc)

# heap_4 coalesces adjacent free blocks, which matters because Mbed TLS
# allocates and frees bignums of varying size for the duration of the product.
# heap_1, which the Qorvo baseline is closest to using, cannot free at all.

# The vendor kernel predates several warnings this repository builds with, and
# it is compiled unmodified on purpose, so they are relaxed for it alone.
target_compile_options(ultrawidelock_kernel PRIVATE
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
add_library(ultrawidelock_board STATIC
  "${ULTRAWIDELOCK_PORT_DIR}/board/startup_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/kernel_hooks_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/tick_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/time_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/fault_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/log_rtt_freertos.c"
  # The OSAL sits here rather than in a layer of its own: it is the port's
  # implementation of the shared modules/ultrawidelock_port contract, it needs nothing but
  # the kernel, and everything above the board uses it.
  "${ULTRAWIDELOCK_PORT_DIR}/osal/osal_freertos.c"
)
target_include_directories(ultrawidelock_board PUBLIC
  "${ULTRAWIDELOCK_PORT_DIR}/include"
  "${ULTRAWIDELOCK_MODULES_DIR}/ultrawidelock_port/include"
)
target_compile_definitions(ultrawidelock_board PUBLIC ULTRAWIDELOCK_PORT_FREERTOS)
target_link_libraries(ultrawidelock_board PUBLIC ultrawidelock_kernel)

# A real cycle, declared rather than worked around. The kernel calls back into
# the board for configASSERT, the stack-overflow hook, and the static memory the
# idle and timer tasks are built from; the board calls into the kernel for
# everything else. CMake resolves a static-library cycle by repeating the
# archives on the link line, which is what this needs.
target_link_libraries(ultrawidelock_kernel PRIVATE ultrawidelock_board)

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
set(ULTRAWIDELOCK_MBEDTLS_DIR "${ULTRAWIDELOCK_NCS_WORKSPACE}/modules/crypto/mbedtls")
if(NOT EXISTS "${ULTRAWIDELOCK_MBEDTLS_DIR}/library/psa_crypto.c")
  message(FATAL_ERROR "pinned Mbed TLS is missing: ${ULTRAWIDELOCK_MBEDTLS_DIR}")
endif()

file(GLOB ULTRAWIDELOCK_MBEDTLS_SOURCES "${ULTRAWIDELOCK_MBEDTLS_DIR}/library/*.c")
list(FILTER ULTRAWIDELOCK_MBEDTLS_SOURCES EXCLUDE REGEX "/(ssl_|x509|net_sockets|debug|mps_)")

add_library(ultrawidelock_mbedtls STATIC
  ${ULTRAWIDELOCK_MBEDTLS_SOURCES}
  "${ULTRAWIDELOCK_PORT_DIR}/crypto/mbedtls_threading_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/crypto/mbedtls_platform_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/crypto/crypto_init_freertos.c"
)
target_include_directories(ultrawidelock_mbedtls PUBLIC
  "${ULTRAWIDELOCK_MBEDTLS_DIR}/include"
  "${ULTRAWIDELOCK_PORT_DIR}/crypto"
)
# The NCS fork keeps the library-internal headers under include/library rather
# than beside the sources, so both are on the private path.
target_include_directories(ultrawidelock_mbedtls PRIVATE
  "${ULTRAWIDELOCK_MBEDTLS_DIR}/library"
  "${ULTRAWIDELOCK_MBEDTLS_DIR}/include/library"
)
target_compile_definitions(ultrawidelock_mbedtls PUBLIC
  MBEDTLS_CONFIG_FILE="mbedtls_config_freertos.h"
  MBEDTLS_PSA_CRYPTO_CONFIG_FILE="psa_crypto_config_freertos.h"
)
target_link_libraries(ultrawidelock_mbedtls PUBLIC ultrawidelock_board)

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
if(NOT ULTRAWIDELOCK_NIMBLE_DIR)
  message(FATAL_ERROR "ULTRAWIDELOCK_NIMBLE_DIR is not set; see ports/freertos-nrf52833/README.md")
endif()
if(NOT EXISTS "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/npl/freertos/src/npl_os_freertos.c")
  message(FATAL_ERROR "ULTRAWIDELOCK_NIMBLE_DIR is not a mynewt-nimble checkout: ${ULTRAWIDELOCK_NIMBLE_DIR}")
endif()

file(GLOB ULTRAWIDELOCK_NIMBLE_SOURCES
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/nimble/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/npl/freertos/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/util/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/services/gap/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/services/gatt/src/*.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/transport/src/*.c"
)

# ble_gatts_lcl.c dumps the local GATT database over Mynewt's console package,
# which this build does not have and does not want: the port's log sink is
# SEGGER RTT. Upstream's own porting examples leave it out for the same reason.
list(REMOVE_ITEM ULTRAWIDELOCK_NIMBLE_SOURCES "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/src/ble_gatts_lcl.c")

# hal_timer.c and the cputime sources are the link layer's timebase. The
# controller here is Nordic's, reached over HCI, so nothing in this image drives
# them and their only effect would be to claim a hardware timer that
# peripherals.yml has already given to someone else.
list(REMOVE_ITEM ULTRAWIDELOCK_NIMBLE_SOURCES
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/nimble/src/hal_timer.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/nimble/src/os_cputime.c"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/nimble/src/os_cputime_pwr2.c")

add_library(ultrawidelock_nimble STATIC ${ULTRAWIDELOCK_NIMBLE_SOURCES})
target_include_directories(ultrawidelock_nimble PUBLIC
  "${ULTRAWIDELOCK_PORT_DIR}/ble/nimble_syscfg"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/nimble/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/porting/npl/freertos/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/util/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/services/gap/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/host/services/gatt/include"
  "${ULTRAWIDELOCK_NIMBLE_DIR}/nimble/transport/include"
)
target_link_libraries(ultrawidelock_nimble PUBLIC ultrawidelock_kernel ultrawidelock_mbedtls)

# Upstream is compiled unmodified and predates some of the warnings this
# repository builds with. -Wundef in particular cannot survive contact with
# NimBLE's headers: its configuration scheme tests MYNEWT_VAL_* and
# NIMBLE_CFG_* names that are meant to evaluate to zero when unset, so the
# warning fires on the mechanism rather than on a mistake. It is relaxed on the
# INTERFACE side as well, because the port's own files that include those
# headers hit the same thing, and it would be worse to drop the warning for the
# whole port than to scope it to the files that talk to NimBLE.
target_compile_options(ultrawidelock_nimble
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
set(ULTRAWIDELOCK_NRFXLIB "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrfxlib")
set(ULTRAWIDELOCK_MPSL_LIB "${ULTRAWIDELOCK_NRFXLIB}/mpsl/lib/nrf52/hard-float/libmpsl.a")
set(ULTRAWIDELOCK_SDC_LIB
  "${ULTRAWIDELOCK_NRFXLIB}/softdevice_controller/lib/nrf52/hard-float/libsoftdevice_controller_peripheral.a")

# MPSL calls a front-end module API unconditionally, so something has to answer
# it even though the DWM3001CDK has no front-end module: the module is a bare
# nRF52833 driving the DW3110, with no PA or LNA in the radio path. The common
# archive is that answer -- it is the dispatch layer, and linking it without any
# of the device-specific FEM archives is how "no FEM" is expressed.
set(ULTRAWIDELOCK_FEM_LIB "${ULTRAWIDELOCK_NRFXLIB}/mpsl/fem/common/lib/nrf52/hard-float/libmpsl_fem_common.a")

foreach(_ultrawidelock_lib "${ULTRAWIDELOCK_MPSL_LIB}" "${ULTRAWIDELOCK_SDC_LIB}" "${ULTRAWIDELOCK_FEM_LIB}")
  if(NOT EXISTS "${_ultrawidelock_lib}")
    message(FATAL_ERROR "pinned radio binary is missing: ${_ultrawidelock_lib}")
  endif()
endforeach()

add_library(ultrawidelock_radio STATIC
  "${ULTRAWIDELOCK_PORT_DIR}/radio/radio_start_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/radio/mpsl_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/entropy_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/board/temperature_freertos.c"
  # The pinned NCS opcode dispatcher, compiled unmodified against ble/hci_compat
  # exactly as make freertos-ncs-source-check compiles it, plus the port half
  # that adapts it. The controller's command API is opcode-specific, so an HCI
  # command has to be decoded before it reaches the controller.
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller/hci_internal.c"
  "${ULTRAWIDELOCK_PORT_DIR}/ble/hci_dispatcher_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/ble/nimble_sdc_transport.c"
  "${ULTRAWIDELOCK_PORT_DIR}/ble/nimble_host_freertos.c"
)
target_include_directories(ultrawidelock_radio PUBLIC
  "${ULTRAWIDELOCK_NRFXLIB}/mpsl/include"
  "${ULTRAWIDELOCK_NRFXLIB}/mpsl/fem/include"
  "${ULTRAWIDELOCK_NRFXLIB}/softdevice_controller/include"
  "${ULTRAWIDELOCK_PORT_DIR}/ble/hci_compat"
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller"
)
target_link_libraries(ultrawidelock_radio PUBLIC ultrawidelock_board ultrawidelock_nimble "${ULTRAWIDELOCK_MPSL_LIB}" "${ULTRAWIDELOCK_SDC_LIB}"
  "${ULTRAWIDELOCK_FEM_LIB}")

# The dispatcher reads its configuration from a forced include, the same way the
# source-check script compiles it, so the vendor file needs no edit.
set_source_files_properties(
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/subsys/bluetooth/controller/hci_internal.c"
  PROPERTIES COMPILE_OPTIONS
  "-include;${ULTRAWIDELOCK_PORT_DIR}/ble/hci_compat/ultrawidelock_freertos_hci_config.h")

# The second declared cycle, and the same reasoning as the kernel's. The vector
# table in board/startup_freertos.c is the only thing that references the radio
# entry points, so without this the archive is scanned before those references
# exist and radio_start_freertos.c is never pulled in.
target_link_libraries(ultrawidelock_board PUBLIC ultrawidelock_radio)

# ----------------------------------------------------------------------------
# Storage layer: the persistent key-value store and its two readers.
#
# board/flash_freertos.c belongs here rather than with the rest of the board,
# because it arbitrates NVMC against the radio with MPSL timeslots and so
# cannot be linked without MPSL.
# ----------------------------------------------------------------------------
add_library(ultrawidelock_storage STATIC
  "${ULTRAWIDELOCK_PORT_DIR}/board/flash_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/storage/kv_flash_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/storage/ultrawidelock_prov_kv.c"
  # ultrawidelock_prov.c -- the portable half of the provisioning record -- was listed
  # here while this was the only layer that needed it. It is manifested in
  # ultrawidelock_cred/roles/reader_policy.list and now comes in with the rest of that
  # role, below: naming it in two places would put one object in two archives.
)
# The provisioning backend implements a contract owned by the shared credential
# module, not by this port, so the module's public headers are on the path.
target_include_directories(ultrawidelock_storage PUBLIC
  "${ULTRAWIDELOCK_MODULES_DIR}/ultrawidelock_cred/include"
)
target_link_libraries(ultrawidelock_storage PUBLIC ultrawidelock_radio)

# ----------------------------------------------------------------------------
# UWB layer: the ranging engine, the vendored decadriver, and the DW3110
# platform backends. Its own file, because its source set comes from the shared
# role manifests rather than from a list kept here.
# ----------------------------------------------------------------------------
include("${ULTRAWIDELOCK_PORT_DIR}/uwb/uwb.cmake")

# ----------------------------------------------------------------------------
# credential reader: the transaction engine, its BLE transport, and the ranging glue.
#
# After the UWB layer because it calls into it: ultrawidelock_ranging.c drives the
# responder through the contract in <ultrawidelock/uwb.h>.
#
# Every source here comes from a role manifest, including the NimBLE transport
# backend -- that file names no OS and is the same one ports/esp32 builds, so it
# is shared rather than copied. Only ble/ultrawidelock_ble_freertos.c is this port's,
# and it is short because ultrawidelock_radio already owns the host: see its header for
# why bonding is off and what turning it on would cost.
# ----------------------------------------------------------------------------
set(ULTRAWIDELOCK_CRED_ROLES "${ULTRAWIDELOCK_MODULES_DIR}/ultrawidelock_cred/roles")
set(ULTRAWIDELOCK_CRED_ROLE_LISTS
  "${ULTRAWIDELOCK_CRED_ROLES}/reader.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/reader_policy.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/ble_transport.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/apdu.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/crypto.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/hash.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/dyntag.list"
  "${ULTRAWIDELOCK_CRED_ROLES}/wire_codecs.list"
)

set(ULTRAWIDELOCK_CRED_SOURCES)
foreach(_ultrawidelock_list IN LISTS ULTRAWIDELOCK_CRED_ROLE_LISTS)
  if(NOT EXISTS "${_ultrawidelock_list}")
    message(FATAL_ERROR "credential role manifest is missing: ${_ultrawidelock_list}")
  endif()
  ultrawidelock_role_sources("${_ultrawidelock_list}" _ultrawidelock_role_srcs)
  list(APPEND ULTRAWIDELOCK_CRED_SOURCES ${_ultrawidelock_role_srcs})
endforeach()

add_library(ultrawidelock_cred STATIC
  ${ULTRAWIDELOCK_CRED_SOURCES}
  "${ULTRAWIDELOCK_PORT_DIR}/ble/ultrawidelock_ble_freertos.c"
)
target_include_directories(ultrawidelock_cred PUBLIC
  "${ULTRAWIDELOCK_MODULES_DIR}/ultrawidelock_cred/include"
)
# ultrawidelock_radio for the NimBLE host and its hooks, ultrawidelock_storage for the provisioning
# backend the identity is read through, ultrawidelock_uwb for the ranging contract.
target_link_libraries(ultrawidelock_cred PUBLIC ultrawidelock_radio ultrawidelock_storage ultrawidelock_uwb)

# ----------------------------------------------------------------------------
# 802.15.4 layer: Nordic's radio driver, its service-layer binary, and the
# platform files this port already supplies in radio/.
#
# Built from an explicit source list rather than through nrfxlib's own CMake.
# That CMake builds five interface targets whose compile definitions come from
# Zephyr Kconfig, and adding it would pull the Kconfig dependency this port
# exists to avoid. The list below is driver/CMakeLists.txt's, read at the pinned
# revision; tests/ports/freertos-nrf52833/nrf_802154_sources_check.sh fails if
# the two drift apart, which is the same guard the UWB manifests get.
#
# The service layer is a binary. It arbitrates the radio with MPSL -- timeslot
# requests, the low-power timer, RSSI and CCA timing -- and there is no source
# form to compile, only the per-core, per-float-ABI archive selected here.
# ----------------------------------------------------------------------------
set(ULTRAWIDELOCK_802154_DIR "${ULTRAWIDELOCK_NRFXLIB}/nrf_802154")
set(ULTRAWIDELOCK_802154_SL_LIB
  "${ULTRAWIDELOCK_802154_DIR}/sl/sl/lib/nrf52833/hard-float/libnrf-802154-sl.a")
if(NOT EXISTS "${ULTRAWIDELOCK_802154_SL_LIB}")
  message(FATAL_ERROR "802.15.4 service layer is missing: ${ULTRAWIDELOCK_802154_SL_LIB}")
endif()

set(ULTRAWIDELOCK_802154_SOURCES)
foreach(_ultrawidelock_src
    nrf_802154.c
    nrf_802154_aes_ccm_acc_ccm.c
    nrf_802154_aes_ccm_acc_ecb.c
    nrf_802154_co.c
    nrf_802154_core.c
    nrf_802154_core_hooks.c
    nrf_802154_critical_section.c
    nrf_802154_debug.c
    nrf_802154_debug_gpio.c
    nrf_802154_encrypt.c
    nrf_802154_notification_swi.c
    nrf_802154_pib.c
    nrf_802154_queue.c
    nrf_802154_request_swi.c
    nrf_802154_rssi.c
    nrf_802154_rx_buffer.c
    nrf_802154_stats.c
    nrf_802154_swi.c
    nrf_802154_swi_callouts_weak.c
    nrf_802154_trx.c
    nrf_802154_trx_ppi.c
    nrf_802154_tx_work_buffer.c
    nrf_802154_tx_power.c
    mac_features/nrf_802154_csma_ca.c
    mac_features/nrf_802154_csma_ca_backoff.c
    mac_features/nrf_802154_delayed_trx.c
    mac_features/nrf_802154_filter.c
    mac_features/nrf_802154_frame_parser.c
    mac_features/nrf_802154_ie_writer.c
    mac_features/nrf_802154_imm_tx.c
    mac_features/nrf_802154_security_pib_ram.c
    mac_features/nrf_802154_security_writer.c
    mac_features/nrf_802154_precise_ack_timeout.c
    mac_features/nrf_802154_tx_timestamp_provider.c
    mac_features/ack_generator/nrf_802154_ack_data.c
    mac_features/ack_generator/nrf_802154_ack_generator.c
    mac_features/ack_generator/nrf_802154_enh_ack_generator.c
    mac_features/ack_generator/nrf_802154_imm_ack_generator.c)
  list(APPEND ULTRAWIDELOCK_802154_SOURCES "${ULTRAWIDELOCK_802154_DIR}/driver/src/${_ultrawidelock_src}")
endforeach()

# Four of upstream's sources are deliberately absent, and each omission is a
# selection rather than a trim:
#
#   nrf_802154_bsim_utils.c      BabbleSim host simulation, not a target build
#   nrf_802154_notification_direct.c \  the direct variants are the SL_OPENSOURCE
#   nrf_802154_request_direct.c      /  build's; this image uses the SWI ones,
#                                       which is what the shared EGU0 handler in
#                                       radio/ is wired for
#   nrf_802154_trx_dppi.c        DPPI belongs to nRF53/nRF91. nRF52833 has PPI,
#                                and linking both would define the same symbols

add_library(ultrawidelock_802154 STATIC
  ${ULTRAWIDELOCK_802154_SOURCES}
  # Shared timestamp conversions, from nrfxlib's common tree rather than the
  # driver's. radio_nrf5.c calls them directly for its CSL and delayed-TX
  # timing, so they belong to the layer even though upstream files them apart.
  "${ULTRAWIDELOCK_802154_DIR}/common/src/nrf_802154_common_utils.c"
  # The platform half of the driver: everything nrfxlib declares under
  # platform/ and leaves to whoever integrates it. Under Zephyr these come
  # from the nrfxlib module glue; here they are the port's, and they are what
  # keeps this driver sharing CLOCK, the low-power timer and the interrupt
  # controller with MPSL instead of fighting it for them.
  "${ULTRAWIDELOCK_PORT_DIR}/radio/nrf_802154_clock_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/radio/nrf_802154_irq_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/radio/nrf_802154_lptimer_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/radio/nrf_802154_misc_freertos.c"
  # No code, only the peripheral collision assertions. Here because this is
  # the only library whose include path reaches both the driver's private
  # masks and the UWB layer's board_pins.h.
  "${ULTRAWIDELOCK_PORT_DIR}/radio/peripheral_asserts_freertos.c"
  # The high-precision timer, a pinned NCS source compiled unmodified. It is an
  # 802.15.4 platform file rather than a Thread one despite living under
  # nrf/modules: the service layer's timer coordinator calls it, and nothing in
  # OpenThread does.
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/nrfxlib/nrf_802154/sl/platform/nrf_802154_hp_timer.c"
)
target_include_directories(ultrawidelock_802154 PUBLIC
  "${ULTRAWIDELOCK_802154_DIR}/common/include"
  "${ULTRAWIDELOCK_802154_DIR}/driver/include"
  "${ULTRAWIDELOCK_802154_DIR}/sl/include"
  "${ULTRAWIDELOCK_802154_DIR}/sl/sl/include"
)
target_include_directories(ultrawidelock_802154 PRIVATE
  "${ULTRAWIDELOCK_802154_DIR}/driver/src"
  "${ULTRAWIDELOCK_PORT_DIR}/uwb"
)
target_compile_definitions(ultrawidelock_802154 PUBLIC
  # This core has the driver; nothing is serialized to a network core.
  NRF_802154_SERIALIZATION_HOST=0
  # The port's configuration, forced in by name the same way Mbed TLS's is, so
  # there is one answer to what this driver was built with.
  NRF_802154_PROJECT_CONFIG="ultrawidelock_freertos_802154_config.h"
)
target_include_directories(ultrawidelock_802154 PUBLIC "${ULTRAWIDELOCK_PORT_DIR}/radio")

# The service layer as an imported target rather than a bare path, so the cycle
# below can be declared. It is a real cycle and not an ordering mistake: the SL
# archive's timer coordinator calls the platform's high-precision timer, and the
# driver's core calls back into the SL for timeslots and timing. A single scan
# in either order leaves the other half unresolved.
add_library(ultrawidelock_802154_sl STATIC IMPORTED)
set_target_properties(ultrawidelock_802154_sl PROPERTIES IMPORTED_LOCATION "${ULTRAWIDELOCK_802154_SL_LIB}")
target_link_libraries(ultrawidelock_802154 PUBLIC ultrawidelock_radio ultrawidelock_802154_sl)
set_property(TARGET ultrawidelock_802154_sl APPEND PROPERTY INTERFACE_LINK_LIBRARIES ultrawidelock_802154)

# Vendor source, compiled unmodified, so the port's warning set is not imposed
# on it -- the same treatment the FreeRTOS kernel gets above.
target_compile_options(ultrawidelock_802154 PRIVATE
  -ffunction-sections
  -fdata-sections
  -fno-common
  -fno-strict-aliasing
  -Wno-unused-parameter
)

# ----------------------------------------------------------------------------
# Thread layer: OpenThread as an MTD, plus the port's platform adapters.
#
# Built through upstream's own CMake with OT_PLATFORM=external, which is the
# supported way to supply a platform from outside the tree: it links ot-config
# and leaves otPlat* to whoever provides them. That is this port.
#
# OT_EXTERNAL_MBEDTLS points at the crypto layer above, so there is one Mbed TLS
# in the image rather than OpenThread bringing its own. This is also where
# leaving OPENTHREAD_CONFIG_CRYPTO_LIB at its upstream MBEDTLS default pays for
# itself: OpenThread compiles its own otPlatCrypto from crypto_platform.cpp
# against that library, so the port implements none of that surface.
# ----------------------------------------------------------------------------
set(ULTRAWIDELOCK_OPENTHREAD_DIR "${ULTRAWIDELOCK_NCS_WORKSPACE}/modules/lib/openthread")
if(NOT EXISTS "${ULTRAWIDELOCK_OPENTHREAD_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "pinned OpenThread is missing: ${ULTRAWIDELOCK_OPENTHREAD_DIR}")
endif()

set(OT_PLATFORM "external" CACHE STRING "" FORCE)
set(OT_BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(OT_MTD ON CACHE BOOL "" FORCE)
set(OT_FTD OFF CACHE BOOL "" FORCE)
set(OT_RCP OFF CACHE BOOL "" FORCE)
set(OT_APP_CLI OFF CACHE BOOL "" FORCE)
set(OT_APP_NCP OFF CACHE BOOL "" FORCE)
set(OT_APP_RCP OFF CACHE BOOL "" FORCE)
set(OT_EXTERNAL_MBEDTLS "ultrawidelock_mbedtls" CACHE STRING "" FORCE)
set(OT_MBEDTLS_THREADING ON CACHE BOOL "" FORCE)
set(OT_PROJECT_CONFIG "openthread_project_config.h" CACHE STRING "" FORCE)

# The project config is found by name, so its directory has to be reachable
# from every OpenThread translation unit.
include_directories("${ULTRAWIDELOCK_PORT_DIR}/thread")

add_subdirectory("${ULTRAWIDELOCK_OPENTHREAD_DIR}" openthread EXCLUDE_FROM_ALL)

add_library(ultrawidelock_thread STATIC
  # Nordic's OpenThread radio platform and the service layer's high-precision
  # timer, both pinned NCS sources compiled unmodified against thread/ot_compat
  # -- exactly as make freertos-ncs-source-check compiles them. They are the
  # otPlatRadio surface: without them OpenThread has no radio and every
  # otPlatRadio* entry point is undefined.
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/openthread/platform/radio_nrf5.c"
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/nrfxlib/nrf_802154/sl/platform/nrf_802154_hp_timer.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/openthread_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_kernel_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_radio_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_alarm_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_misc_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_log_freertos.c"
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_settings_freertos.c"
)
target_include_directories(ultrawidelock_thread PUBLIC
  "${ULTRAWIDELOCK_PORT_DIR}/thread/ot_compat"
  "${ULTRAWIDELOCK_OPENTHREAD_DIR}/include"
  "${ULTRAWIDELOCK_OPENTHREAD_DIR}/examples/platforms"
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/openthread/platform"
)
target_link_libraries(ultrawidelock_thread PUBLIC ultrawidelock_storage ultrawidelock_802154 openthread-mtd ot-config)

# The third declared cycle in this file, and the same shape as the other two.
# OpenThread's core calls otPlatSettings*, otPlatLog and otPlatReset, which this
# port implements in thread/; this port calls into OpenThread's core. Whichever
# archive is scanned first leaves the other's references open.
set_property(TARGET openthread-mtd APPEND PROPERTY INTERFACE_LINK_LIBRARIES ultrawidelock_thread)

# The two vendor sources read their configuration from a forced include, the
# same way the source-check script compiles them, so neither needs an edit.
set_source_files_properties(
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/openthread/platform/radio_nrf5.c"
  "${ULTRAWIDELOCK_NCS_WORKSPACE}/nrf/modules/nrfxlib/nrf_802154/sl/platform/nrf_802154_hp_timer.c"
  PROPERTIES COMPILE_OPTIONS
  "-include;${ULTRAWIDELOCK_PORT_DIR}/thread/ot_compat/ultrawidelock_freertos_ot_config.h;-Wno-unused-parameter")

add_library(ultrawidelock_freertos_port INTERFACE)
target_link_libraries(ultrawidelock_freertos_port INTERFACE ultrawidelock_storage ultrawidelock_uwb ultrawidelock_thread ultrawidelock_cred)

# ----------------------------------------------------------------------------
# Warnings and code generation, applied to the port's own code only.
# ----------------------------------------------------------------------------
foreach(_ultrawidelock_target ultrawidelock_board ultrawidelock_radio ultrawidelock_storage ultrawidelock_thread)
  target_compile_options(${_ultrawidelock_target} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wundef
  )
endforeach()

# The credential BLE layer gets the same treatment minus -Wundef, which it cannot
# have: NimBLE's public headers test MYNEWT_VAL_* names that are meant to
# evaluate to zero when unset, and ultrawidelock_nimble already relaxes it PUBLIC for that
# reason. The rest still applies -- this is the port's own code and the shared
# backend, not vendor source.
target_compile_options(ultrawidelock_cred PRIVATE
  -Wall
  -Wextra
  -Werror
  -Wshadow
)

foreach(_ultrawidelock_target ultrawidelock_soc ultrawidelock_kernel ultrawidelock_board ultrawidelock_radio ultrawidelock_nimble ultrawidelock_mbedtls
                    ultrawidelock_storage ultrawidelock_uwb ultrawidelock_cred)
  target_compile_options(${_ultrawidelock_target} PRIVATE
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
function(ultrawidelock_freertos_image target)
  target_link_options(${target} PRIVATE
    "-T${ULTRAWIDELOCK_PORT_DIR}/board/nrf52833_lock.ld"
    "-L${ULTRAWIDELOCK_PORT_DIR}/board"
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map"
  )
  set_target_properties(${target} PROPERTIES
    LINK_DEPENDS "${ULTRAWIDELOCK_PORT_DIR}/board/nrf52833_lock.ld"
    SUFFIX ".elf"
  )
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${target}>"
            "$<TARGET_FILE_DIR:${target}>/${target}.hex"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>"
    VERBATIM
  )
endfunction()
