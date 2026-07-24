/* matterfake freertos/FreeRTOS.h — shadows the sdkfake one: the matter-lock
 * app needs task notifications, ISR-context probes, and the ESP-IDF spelling
 * of portYIELD_FROM_ISR(woken). All recording doubles (matterfake.cc). */
#ifndef MATTERFAKE_FREERTOS_H
#define MATTERFAKE_FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0

#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

int mfk_port_in_isr(void);
void mfk_port_yield_from_isr(BaseType_t woken);
#define xPortInIsrContext() mfk_port_in_isr()
#define portYIELD_FROM_ISR(woken) mfk_port_yield_from_isr(woken)

#endif /* MATTERFAKE_FREERTOS_H */
