/*
 * The nRF5 SDK header the pinned FreeRTOS Cortex-M4F port includes, reduced to
 * what that port actually uses.
 *
 * portable/CMSIS/nrf52/portmacro_cmsis.h includes "app_util.h" and reaches for
 * exactly one thing in it: ROUNDED_DIV, inside the RTC prescaler macro. The
 * real header pulls in the whole nRF5 SDK utility layer -- error codes, atomic
 * helpers, the SoftDevice critical-region API -- none of which this port has or
 * wants, because it builds against MPSL and nrfx rather than against that SDK.
 *
 * Supplying the one macro is what lets the vendor port compile unmodified,
 * which is the same arrangement ble/hci_compat and thread/ot_compat use for the
 * pinned NCS sources. If a future kernel revision starts using more of this
 * header, the build breaks here rather than silently picking up a second SDK.
 */
#ifndef WOZ_FREERTOS_KERNEL_COMPAT_APP_UTIL_H
#define WOZ_FREERTOS_KERNEL_COMPAT_APP_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ROUNDED_DIV
#define ROUNDED_DIV(A, B) (((A) + ((B) / 2)) / (B))
#endif

#endif /* WOZ_FREERTOS_KERNEL_COMPAT_APP_UTIL_H */
