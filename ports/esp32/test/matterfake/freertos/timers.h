/* matterfake freertos/timers.h — door_lock_manager.h includes it; nothing in
 * the tested sources touches software timers. */
#ifndef MATTERFAKE_FREERTOS_TIMERS_H
#define MATTERFAKE_FREERTOS_TIMERS_H

#include "FreeRTOS.h"

typedef struct mfk_timer_opaque *TimerHandle_t;

#endif /* MATTERFAKE_FREERTOS_TIMERS_H */
