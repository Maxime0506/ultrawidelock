/*
 * Entry point for the DWM3001CDK lock.
 *
 * Startup order here is not arbitrary and is the thing to preserve as the rest
 * of the product is added below it.
 *
 * The radio starts first, before the scheduler. MPSL owns CLOCK and starts the
 * low-frequency crystal inside mpsl_init(); RTC1 carries the FreeRTOS tick and
 * counts from that same crystal. Starting the scheduler first would mean
 * starting it on a clock that is not running yet, which board/tick_freertos.c
 * turns into a named fatal rather than a hang, but which is still a board that
 * never boots.
 *
 * Creating tasks before the scheduler runs is legal, and the notifications
 * MPSL's low-priority handler posts in the meantime are latched and delivered
 * once it starts, so nothing is lost in the window.
 *
 * A failed radio start is fatal here rather than degraded. A lock with no radio
 * cannot be opened by any of the three ways this product supports, so coming up
 * far enough to log the reason and reset is more useful than coming up far
 * enough to look healthy.
 */
#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_crypto.h>
#include <woz_freertos_nimble_host.h>
#include <woz_freertos_platform.h>
#include <woz_freertos_radio.h>

#define MAIN_TAG "main"

static StaticTask_t s_boot_tcb;
static StackType_t s_boot_stack[512];

/*
 * A placeholder for the product task. It exists so the first images have a task
 * to schedule and so the tick can be watched advancing on the bench before any
 * of the BLE, UWB, or Aliro layers are trusted.
 */
static void boot_task(void *arg)
{
	(void)arg;

	woz_freertos_log(WOZ_FREERTOS_LOG_INFO, MAIN_TAG, "controller pool used: %u bytes",
			 (unsigned)woz_freertos_radio_memory_used());

	/*
	 * The BLE host comes up on the scheduler, unlike the radio: it has a
	 * task of its own, it waits for the controller to answer a reset, and
	 * its porting layer allocates from the heap. None of that works before
	 * vTaskStartScheduler(), which is why this is here and not in main().
	 */
	if (woz_freertos_nimble_host_start() != 0) {
		woz_freertos_fatal("BLE host start failed");
	}

	for (;;) {
		woz_freertos_log(WOZ_FREERTOS_LOG_INFO, MAIN_TAG, "uptime %lld us, radio %s",
				 (long long)woz_freertos_uptime_us(),
				 woz_freertos_radio_ready() ? "ready" : "down");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

int main(void)
{
	int err;

	/*
	 * Crypto first, because it is the one bring-up step that can be retried:
	 * woz_freertos_crypto_init() logs and returns non-zero rather than
	 * halting, so a transient PSA failure does not cost the whole boot. It
	 * needs no scheduler and no radio.
	 */
	(void)woz_freertos_crypto_init();

	err = woz_freertos_radio_start(woz_freertos_radio_sdc_dispatcher());
	if (err != 0) {
		/*
		 * The value is the negated woz_freertos_radio_stage that failed,
		 * which is the one piece of information worth carrying into the
		 * log: the stages fail for quite different reasons.
		 */
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, MAIN_TAG, "radio start failed at stage %d",
				 -err);
		woz_freertos_fatal("radio start failed");
	}

	(void)xTaskCreateStatic(boot_task, "boot",
				(uint32_t)(sizeof(s_boot_stack) / sizeof(s_boot_stack[0])), NULL, 1,
				s_boot_stack, &s_boot_tcb);

	vTaskStartScheduler();

	woz_freertos_fatal("scheduler returned");
}
