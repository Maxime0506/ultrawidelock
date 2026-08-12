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

add_library(woz_freertos_port INTERFACE)
target_link_libraries(woz_freertos_port INTERFACE woz_board)

# ----------------------------------------------------------------------------
# Warnings and code generation, applied to the port's own code only.
# ----------------------------------------------------------------------------
foreach(_woz_target woz_board)
  target_compile_options(${_woz_target} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wundef
  )
endforeach()

foreach(_woz_target woz_device woz_kernel woz_board)
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
