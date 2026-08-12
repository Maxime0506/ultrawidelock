# Cross toolchain for the nRF52833 target build.
#
# The compiler is located by name on PATH unless WOZ_ARM_TOOLCHAIN_DIR points at
# a bin directory, which is how a toolchain installed outside the system prefix
# is used without putting it on PATH for everything else.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{WOZ_ARM_TOOLCHAIN_DIR} AND NOT WOZ_ARM_TOOLCHAIN_DIR)
  set(WOZ_ARM_TOOLCHAIN_DIR "$ENV{WOZ_ARM_TOOLCHAIN_DIR}")
endif()

if(WOZ_ARM_TOOLCHAIN_DIR)
  set(_woz_tc_prefix "${WOZ_ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
  set(_woz_tc_prefix "arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER "${_woz_tc_prefix}gcc")
set(CMAKE_ASM_COMPILER "${_woz_tc_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_woz_tc_prefix}g++")
set(CMAKE_OBJCOPY "${_woz_tc_prefix}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_SIZE "${_woz_tc_prefix}size" CACHE FILEPATH "size")

# There is no OS to link against, so the usual "compile and run" probe cannot
# work; a static library probe is the supported way to validate a bare-metal
# compiler.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# nRF52833 is a Cortex-M4 with a single-precision FPU. The hard float ABI is not
# optional here: the pinned FreeRTOS port refuses to build without __FPU_USED,
# and the MPSL and SoftDevice Controller binaries are compiled for it.
set(WOZ_ARCH_FLAGS "-mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=hard -mfpu=fpv4-sp-d16")

set(CMAKE_C_FLAGS_INIT "${WOZ_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${WOZ_ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${WOZ_ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${WOZ_ARCH_FLAGS}")
