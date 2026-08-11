/*
 * FreeRTOS ownership and serialization for one upstream OpenThread instance.
 *
 * The nRF52833 platform supplies radio, alarm, settings, entropy, and flash
 * drivers. Each driver must make its pending work visible through
 * woz_freertos_openthread_process_drivers() and wake this task when work
 * becomes ready.
 */
#ifndef WOZ_FREERTOS_OPENTHREAD_H
#define WOZ_FREERTOS_OPENTHREAD_H

#include <stdbool.h>

typedef struct otInstance otInstance;

/** Start the single static OpenThread task. Returns zero on success. */
int woz_freertos_openthread_start(otInstance *instance);

/** The instance owned by the task, or NULL before a successful start. */
otInstance *woz_freertos_openthread_instance(void);

/** Serialize OpenThread API calls made outside the OpenThread task. */
void woz_freertos_openthread_lock(void);
void woz_freertos_openthread_unlock(void);

/** Wake the OpenThread task after a platform driver records pending work. */
void woz_freertos_openthread_wake(void);

/** ISR-safe counterpart used by the radio and alarm interrupt paths. */
void woz_freertos_openthread_wake_from_isr(void);

/**
 * Drain pending nRF52833 OpenThread platform-driver work without blocking.
 *
 * Supplied by the radio-platform slice. It is called with the recursive
 * OpenThread API lock held, so callbacks may safely re-enter the public API.
 */
void woz_freertos_openthread_process_drivers(otInstance *instance);

/**
 * Bring the 802.15.4 radio platform up. Returns zero on success.
 *
 * The driver arbitrates the radio through MPSL, so this must follow
 * woz_freertos_radio_start(), and it must precede
 * woz_freertos_openthread_start() because the task drains the platform on
 * every pass.
 */
int woz_freertos_openthread_radio_start(void);

/** Whether the radio platform has been started. */
bool woz_freertos_openthread_radio_started(void);

#endif /* WOZ_FREERTOS_OPENTHREAD_H */
