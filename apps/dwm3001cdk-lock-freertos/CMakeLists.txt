# The DWM3001CDK lock image.
#
# The application is still a skeleton: it brings the kernel up and proves the
# tick runs. What it is for right now is the link, because the binding
# constraint on this port is the 512 KB flash and 128 KB RAM budget and the
# Zephyr oracle already overflows 128 KB by 1,752 bytes with the same feature
# set. Every layer added below the application gets measured here as it lands.

cmake_minimum_required(VERSION 3.20)

project(dwm3001cdk_lock_freertos C ASM)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../ports/freertos-nrf52833" port)

add_executable(dwm3001cdk-lock-freertos
  "${CMAKE_CURRENT_LIST_DIR}/src/main.c"
  "${CMAKE_CURRENT_LIST_DIR}/src/radio_not_yet_linked.c"
)
target_link_libraries(dwm3001cdk-lock-freertos PRIVATE woz_freertos_port)
target_compile_options(dwm3001cdk-lock-freertos PRIVATE -Wall -Wextra -Werror)

woz_freertos_image(dwm3001cdk-lock-freertos)
