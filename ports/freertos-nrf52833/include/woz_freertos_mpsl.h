/* FreeRTOS serialization and low-priority worker for Nordic MPSL. */
#ifndef WOZ_FREERTOS_MPSL_H
#define WOZ_FREERTOS_MPSL_H

#include <stdbool.h>

/**
 * Create the shared MPSL lock and static low-priority processing task.
 * Call this before mpsl_init(), then route the selected low-priority IRQ to
 * woz_freertos_mpsl_wake_from_isr().
 */
int woz_freertos_mpsl_start(void (*low_priority_process)(void));

/** True after the shared lock and processing task are ready. */
bool woz_freertos_mpsl_ready(void);

/** Serialize all application, BLE, and 802.15.4 low-priority MPSL calls. */
void woz_freertos_mpsl_lock(void);
void woz_freertos_mpsl_unlock(void);

/** Schedule MPSL low-priority work from task or interrupt context. */
void woz_freertos_mpsl_wake(void);
void woz_freertos_mpsl_wake_from_isr(void);

#endif /* WOZ_FREERTOS_MPSL_H */
