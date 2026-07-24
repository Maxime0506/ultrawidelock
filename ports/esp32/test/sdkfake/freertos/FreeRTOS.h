/* sdkfake freertos/FreeRTOS.h — types, macros, and the fake_freertos.c control
 * surface. Everything is synchronous: nothing here schedules anything. */
#ifndef SDKFAKE_FREERTOS_H
#define SDKFAKE_FREERTOS_H

#include "../sdkfake.h"

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE 0
#define pdTRUE  1
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY     0xFFFFFFFFu
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

/* Spinlocks collapse to a plain int; enter/exit only count nesting. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern int fake_portmux_depth;
#define portENTER_CRITICAL(mux) do { (void)(mux); fake_portmux_depth++; } while (0)
#define portEXIT_CRITICAL(mux)  do { (void)(mux); fake_portmux_depth--; } while (0)
#define portYIELD_FROM_ISR()    do { fake_port_yields++; } while (0)
extern int fake_port_yields;

/* ---- fake_freertos.c control surface ---- */
struct fake_task {
	void (*fn)(void *);
	void *arg;
	char name[24];
	UBaseType_t prio;
	int core; /* -1 = unpinned */
};
#define FAKE_TASK_MAX 8
extern struct fake_task fake_tasks[FAKE_TASK_MAX];
extern int fake_task_count;
extern int fake_task_create_rc; /* pdPASS unless a test injects pdFAIL */

extern int fake_delay_calls;
extern TickType_t fake_delay_total_ticks;
/* Optional: called on every vTaskDelay (e.g. longjmp out of app_main's loop). */
extern void (*fake_delay_hook)(void);

void fake_freertos_reset(void);

#endif
