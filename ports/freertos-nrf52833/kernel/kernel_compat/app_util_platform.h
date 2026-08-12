/*
 * The second nRF5 SDK header portable/CMSIS/nrf52/port_cmsis.c includes.
 *
 * The port includes it and uses nothing from it: the SoftDevice-aware pieces it
 * exists for -- the _PRIO_APP_* interrupt priority names and the
 * CRITICAL_REGION_ENTER/EXIT pair -- are only reached under SOFTDEVICE_PRESENT,
 * which this build does not define. The radio here is MPSL and the SoftDevice
 * Controller, not a linked SoftDevice image, so board/FreeRTOSConfig.h states
 * its interrupt priorities as plain numbers against peripherals.yml instead of
 * borrowing that scheme.
 *
 * Empty on purpose, then, and present only so the vendor file compiles
 * unmodified.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_KERNEL_COMPAT_APP_UTIL_PLATFORM_H
#define ULTRAWIDELOCK_FREERTOS_KERNEL_COMPAT_APP_UTIL_PLATFORM_H

#include "app_util.h"

#endif /* ULTRAWIDELOCK_FREERTOS_KERNEL_COMPAT_APP_UTIL_PLATFORM_H */
