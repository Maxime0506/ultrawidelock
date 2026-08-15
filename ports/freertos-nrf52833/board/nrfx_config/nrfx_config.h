/* SPDX-License-Identifier: ISC */

/*
 * The nrfx integration configuration for this port.
 *
 * Almost nothing is enabled. The port uses nrfx for its register-level hal
 * headers -- nrf_rtc.h, nrf_nvmc.h, nrf_rng.h and their neighbours -- and not
 * for its drivers, because every peripheral in peripherals.yml already has an
 * owner that programs it directly. Enabling an nrfx driver for one of them
 * would put a second writer on the same registers.
 *
 * The hardware-resource reservations below are the exception, and they are the
 * reason this file is worth having: they are what makes a PPI channel claimed
 * by application code and already owned by the radio a build failure instead of
 * an intermittent radio fault.
 */
/*
 * nrfx checks for this exact guard name before letting its own common config be
 * included, so the guard is nrfx's rather than this port's.
 */
#ifndef NRFX_CONFIG_H__
#define NRFX_CONFIG_H__

/*
 * MPSL publishes the channels it owns in mpsl_hwres.h. Pulling the number from
 * the header rather than restating it means a controller update that claims a
 * different channel is caught by the compiler.
 *
 * The guard is here because the board layer builds before the radio layer is in
 * the graph; once MPSL is linked the header is always found.
 */
#if defined(__has_include)
#if __has_include(<mpsl_hwres.h>)
#include <mpsl_hwres.h>
#define NRFX_PPI_CHANNELS_USED MPSL_RESERVED_PPI_CHANNELS
#endif
#endif

#ifndef NRFX_PPI_CHANNELS_USED
#define NRFX_PPI_CHANNELS_USED 0
#endif
#ifndef NRFX_PPI_GROUPS_USED
#define NRFX_PPI_GROUPS_USED 0
#endif

/*
 * EGU0 belongs to the nRF 802.15.4 driver and EGU5 to MPSL's low-priority
 * signal, so neither may be handed to an nrfx driver.
 */
#define NRFX_EGUS_USED ((1UL << 0) | (1UL << 5))

/* TIMER0 is MPSL's and TIMER1 is the 802.15.4 high-precision timer's. */
#define NRFX_TIMERS_USED ((1UL << 0) | (1UL << 1))

/*
 * GPIOTE channel 0 is the DW3110 interrupt line and 1 is SW2. Both are
 * programmed directly by their owners rather than through nrfx_gpiote, but they
 * are reserved here so that an nrfx driver added later cannot allocate one out
 * from under them. radio/peripheral_asserts_freertos.c checks the same two
 * against the 802.15.4 driver's masks, which this file cannot see.
 */
#define NRFX_GPIOTE_CHANNELS_USED ((1UL << 0) | (1UL << 1))
#define NRFX_DPPI_CHANNELS_USED 0
#define NRFX_DPPI_GROUPS_USED 0

/*
 * USBD is deliberately absent, and the absence is the point. The USB stack is
 * built as ultrawidelock_usb against the nRF5 SDK's own nrfx on include paths private to
 * that library -- see the note in CMakeLists.txt -- so nothing configured here
 * reaches it, and enabling it here would compile a SECOND USBD driver from the
 * pinned nrfx into the same image.
 */

#include <templates/nrfx_config_common.h>
#include <nrfx_config_nrf52833.h>

#endif /* NRFX_CONFIG_H__ */
