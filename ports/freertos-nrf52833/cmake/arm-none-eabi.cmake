# Cross toolchain for the nRF52833 target build.
#
# WOZ_ARM_TOOLCHAIN_DIR names a bin directory and wins over PATH, which is how a
# toolchain installed outside the system prefix is used without putting it on
# PATH for everything else. Without it the compiler is found by name on PATH.
#
# Whichever is used is then checked for a C library, because the most likely
# arm-none-eabi-gcc on a developer machine cannot build firmware. Homebrew's
# formula of that name is bare GCC with no newlib: no nosys.specs, no libc to
# resolve. It compiles every file in the image and fails only at the link, with
# an error that names missing symbols rather than the missing toolchain. The
# probe below turns that into one sentence at configure time.

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

# The C library probe described at the top of this file.
#
# CMAKE_TRY_COMPILE_TARGET_TYPE above is STATIC_LIBRARY, which is correct for a
# bare-metal compiler and is also exactly why a newlib-less toolchain sails
# through the standard check: archiving never touches libc. So the probe here
# links, with the same specs the image links with, and looks for the file the
# broken toolchain does not ship.
#
# It runs once per configure and is cached, so it costs nothing on a rebuild.
if(NOT DEFINED WOZ_ARM_TOOLCHAIN_HAS_LIBC)
  execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-file-name=nosys.specs
    OUTPUT_VARIABLE _woz_nosys
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _woz_nosys_result
  )
  # -print-file-name echoes the name back unchanged when it cannot find it.
  if(_woz_nosys_result EQUAL 0 AND NOT _woz_nosys STREQUAL "nosys.specs")
    set(WOZ_ARM_TOOLCHAIN_HAS_LIBC TRUE CACHE INTERNAL "arm-none-eabi ships a C library")
  else()
    set(WOZ_ARM_TOOLCHAIN_HAS_LIBC FALSE CACHE INTERNAL "arm-none-eabi ships a C library")
  endif()
endif()

if(NOT WOZ_ARM_TOOLCHAIN_HAS_LIBC)
  message(FATAL_ERROR
    "${CMAKE_C_COMPILER} has no C library: nosys.specs is missing, so this "
    "toolchain can compile the image but not link it.\n"
    "This is what Homebrew's arm-none-eabi-gcc formula installs -- bare GCC "
    "with no newlib. Install the Arm GNU Toolchain instead and point "
    "WOZ_ARM_TOOLCHAIN_DIR at its bin directory, which takes precedence over "
    "whatever is on PATH.")
endif()
