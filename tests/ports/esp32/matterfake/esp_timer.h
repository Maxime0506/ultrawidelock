/* matterfake esp_timer.h — scripted monotonic clock (mfk_now_us). */
#ifndef MATTERFAKE_ESP_TIMER_H
#define MATTERFAKE_ESP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* MATTERFAKE_ESP_TIMER_H */
