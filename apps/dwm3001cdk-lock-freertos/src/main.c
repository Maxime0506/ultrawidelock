/*
 * Entry point for the DWM3001CDK lock.
 *
 * Startup order here is not arbitrary and is the thing to preserve as the rest
 * of the product is added below it:
 *
 *   1. The radio starts first, before the scheduler. MPSL owns CLOCK and starts
 *      the low-frequency crystal inside mpsl_init(); RTC1 carries the FreeRTOS
 *      tick and counts from that same crystal. Starting the scheduler first
 *      would mean starting it on a clock that is not running yet, which shows
 *      up as a board that reaches vTaskStartScheduler() and never ticks.
 *
 *   2. Tasks are created but the scheduler is not yet running during step 1.
 *      That is legal, and the notifications MPSL's low-priority handler posts
 *      in the meantime are latched and delivered when the scheduler starts.
 *
 * The radio layer is not in the build graph yet, so step 1 is currently a
 * declared dependency rather than a call. Until it is, the tick is started
 * against whatever the reset state of the clock leaves running, and
 * tick_freertos.c fails loudly instead of hanging if that is nothing.
 */
#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_platform.h>

#define MAIN_TAG "main"

static StaticTask_t s_boot_tcb;
static StackType_t s_boot_stack[512];

/*
 * A placeholder for the product task. It exists so the first link has a task to
 * schedule and so the tick can be observed advancing on the bench before any of
 * the radio, UWB, or Aliro layers are trusted.
 */
static void boot_task(void *arg)
{
	(void)arg;

	for (;;) {
		woz_freertos_log(WOZ_FREERTOS_LOG_INFO, MAIN_TAG, "uptime %lld us",
				 (long long)woz_freertos_uptime_us());
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

int main(void)
{
	(void)xTaskCreateStatic(boot_task, "boot", (uint32_t)(sizeof(s_boot_stack) / sizeof(s_boot_stack[0])),
			       NULL, 1, s_boot_stack, &s_boot_tcb);

	vTaskStartScheduler();

	woz_freertos_fatal("scheduler returned");
}
