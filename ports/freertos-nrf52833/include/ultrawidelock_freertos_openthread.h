/*
 * FreeRTOS ownership and serialization for one upstream OpenThread instance.
 *
 * The nRF52833 platform supplies radio, alarm, settings, entropy, and flash
 * drivers. Each driver must make its pending work visible through
 * ultrawidelock_freertos_openthread_process_drivers() and wake this task when work
 * becomes ready.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_OPENTHREAD_H
#define ULTRAWIDELOCK_FREERTOS_OPENTHREAD_H

#include <stdbool.h>

typedef struct otInstance otInstance;

/** Start the single static OpenThread task. Returns zero on success. */
int ultrawidelock_freertos_openthread_start(otInstance *instance);

/** The instance owned by the task, or NULL before a successful start. */
otInstance *ultrawidelock_freertos_openthread_instance(void);

/** Serialize OpenThread API calls made outside the OpenThread task. */
void ultrawidelock_freertos_openthread_lock(void);
void ultrawidelock_freertos_openthread_unlock(void);

/** Wake the OpenThread task after a platform driver records pending work. */
void ultrawidelock_freertos_openthread_wake(void);

/** ISR-safe counterpart used by the radio and alarm interrupt paths. */
void ultrawidelock_freertos_openthread_wake_from_isr(void);

/**
 * Drain pending nRF52833 OpenThread platform-driver work without blocking.
 *
 * Supplied by the radio-platform slice. It is called with the recursive
 * OpenThread API lock held, so callbacks may safely re-enter the public API.
 */
void ultrawidelock_freertos_openthread_process_drivers(otInstance *instance);

/**
 * Bring the 802.15.4 radio platform up. Returns zero on success.
 *
 * The driver arbitrates the radio through MPSL, so this must follow
 * ultrawidelock_freertos_radio_start(), and it must precede
 * ultrawidelock_freertos_openthread_start() because the task drains the platform on
 * every pass.
 */
int ultrawidelock_freertos_openthread_radio_start(void);

/** Whether the radio platform has been started. */
bool ultrawidelock_freertos_openthread_radio_started(void);

/**
 * Deliver an expired OpenThread alarm. Called from the drain above, on the
 * OpenThread task with the API lock held, so the stack may re-arm the alarm
 * from inside its own callback.
 */
void ultrawidelock_freertos_openthread_alarm_process(otInstance *instance);

#endif /* ULTRAWIDELOCK_FREERTOS_OPENTHREAD_H */
