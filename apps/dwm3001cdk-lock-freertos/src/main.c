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
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_gpio.h>

#include <FreeRTOS.h>
#include <task.h>

#include <aliro_prov.h>
#include <ultrawidelock/reader.h>

#include <openthread/instance.h>

#include <woz_freertos_board.h>
#include <woz_freertos_crypto.h>
#include <woz_freertos_dfu.h>
#include <woz_freertos_usb.h>
#include <woz_freertos_nimble_host.h>
#include <woz_freertos_openthread.h>
#include <woz_freertos_platform.h>
#include <woz_freertos_kv.h>
#include <woz_freertos_radio.h>
#include <woz_freertos_uwb.h>

#define MAIN_TAG "main"

/*
 * The reader identity and its trust store, static because they outlive the
 * boot task and are large enough that a stack copy would dominate it.
 */
static struct aliro_reader_identity s_identity;
static struct aliro_trust_store s_trust;

/* Latched in main() from SW2 before the scheduler starts; see the note there. */
static bool s_prov_mode;

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
	 * The persistent store, and the reader identity that lives in it.
	 *
	 * This runs on the scheduler because the flash driver arbitrates NVMC
	 * against the radio with MPSL timeslots and waits on them, and it runs
	 * before the BLE host because the identity is what the host will
	 * eventually advertise.
	 *
	 * Neither failure is fatal. woz_freertos_kv_init() reformats a store it
	 * cannot read, and aliro_prov_load() yields a usable development
	 * identity on every failure path, because a reader that will not boot is
	 * worse than one that boots unprovisioned.
	 */
	if (woz_freertos_kv_init() != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, MAIN_TAG, "key-value store unavailable");
	}
	if (aliro_prov_load(&s_identity, &s_trust) != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, MAIN_TAG,
				 "no stored identity; running on the development one");
	}

	/*
	 * Provisioning mode: SW2 held through reset.
	 *
	 * Sampled in main() before the scheduler, latched into s_prov_mode, and
	 * acted on here -- because this is where the store is up and where a
	 * task exists to block on a serial port.
	 *
	 * NOTHING GOES ON AIR on this path, and it does not return. An operator
	 * typing an identity into a lock that is also advertising it is the one
	 * situation where the half-written state is reachable from outside the
	 * board; refusing to be a lock at all while being provisioned removes the
	 * question. The way out is a reset without the button, which is also how
	 * the operator confirms the import took.
	 *
	 * ONE HONEST DIFFERENCE from the Zephyr image, whose provisioning mode
	 * starts no radio at all: MPSL is already running by the time this line
	 * is reached, because main() starts it before the scheduler and because
	 * USB needs the crystal that MPSL arbitrates. MPSL by itself transmits
	 * nothing -- the controller is never told to advertise, the 802.15.4
	 * driver and the DW3110 are never started, and the Aliro reader never
	 * runs. What the two images share is the property that matters: in
	 * provisioning mode the board is not reachable over any radio.
	 */
	if (s_prov_mode) {
		woz_freertos_log(WOZ_FREERTOS_LOG_INFO, MAIN_TAG,
				 "provisioning mode: radios stay down, console on USB");
		if (woz_freertos_usb_start() != 0) {
			woz_freertos_fatal("provisioning console unavailable");
		}
		woz_freertos_prov_console_run();
	}

	/*
	 * Thread.
	 *
	 * The radio driver comes up first and separately, because the two fail
	 * for different reasons and at different costs. Bringing up the nRF
	 * 802.15.4 driver is a peripheral claim that either succeeds or says
	 * which peripheral it lost; otInstanceInitSingle() after it is an
	 * allocation out of OpenThread's own pools. Reporting them as one step
	 * would turn "the radio is not there" and "the stack did not fit" into
	 * the same log line.
	 *
	 * Not fatal, for the same reason UWB is not: Thread is one of the paths
	 * into this lock, and BLE carries the others. A board whose Thread is
	 * down is still a lock.
	 */
	if (woz_freertos_openthread_radio_start() != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, MAIN_TAG,
				 "802.15.4 radio unavailable; Thread will not run");
	} else {
		otInstance *ot = otInstanceInitSingle();

		if (ot == NULL) {
			woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, MAIN_TAG,
					 "OpenThread instance allocation failed");
		} else if (woz_freertos_openthread_start(ot) != 0) {
			woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, MAIN_TAG,
					 "OpenThread task did not start");
		}
	}

	/*
	 * The DW3110, before the BLE host rather than after it.
	 *
	 * Ordering is for the bench, not for correctness: the two radios share
	 * no peripheral, so either order works. Bringing UWB up first means a
	 * board with a broken SPI line or an unconnected reset says so in the
	 * first few lines of the boot log, instead of after the BLE host has
	 * finished announcing itself.
	 *
	 * Not fatal, unlike the controller start in main(). A lock whose UWB is
	 * down still opens by the other two paths this product supports, and a
	 * board that refuses to boot cannot tell anyone which step failed.
	 */
	if (woz_freertos_uwb_start() != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, MAIN_TAG,
				 "UWB unavailable; ranging will not be offered");
	}

	/*
	 * The update channel, registered BEFORE the reader and not after.
	 *
	 * That ordering is the whole reason this line is here rather than
	 * further down: aliro_reader_start() starts the NimBLE host, and every
	 * GATT service in this image has to be registered inside that startup
	 * sequence -- after the memory pools exist, before the host task begins
	 * consuming events. A layer that registered afterwards would be adding
	 * services to a running host.
	 *
	 * Not fatal. An update channel that failed to register leaves a board
	 * that still opens doors and can still be recovered over SWD, and
	 * refusing to boot over it would take away the working half too.
	 */
	if (dfu_ble_start() != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, MAIN_TAG,
				 "no update channel; this board can only be updated by cable");
	}

	/*
	 * The Aliro reader, which brings the BLE host up underneath itself.
	 *
	 * This is the whole product on one line: aliro_reader_start() does the
	 * crypto, loads the provisioned identity, arms the UWB ranging adapter,
	 * and hands its transport config to the port's aliro_ble_start(), which
	 * registers the GATT service and starts the controller and host.
	 *
	 * It runs on the scheduler, unlike the radio: the host has a task of its
	 * own, it waits for the controller to answer a reset, and NimBLE's
	 * porting layer allocates from the heap. None of that works before
	 * vTaskStartScheduler(), which is why this is here and not in main().
	 *
	 * Fatal, unlike UWB above. The reader is the product: a lock that cannot
	 * advertise cannot be unlocked by any path, so there is nothing to
	 * degrade to and a silent boot would be the worst outcome on a bench.
	 */
	if (aliro_reader_start() != 0) {
		woz_freertos_fatal("Aliro reader start failed");
	}

	for (;;) {
		woz_freertos_log(WOZ_FREERTOS_LOG_INFO, MAIN_TAG, "uptime %lld us, radio %s, uwb %s",
				 (long long)woz_freertos_uptime_us(),
				 woz_freertos_radio_ready() ? "ready" : "down",
				 woz_freertos_uwb_ready() ? "ready" : "down");
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

	/*
	 * SW2, sampled here because "held through reset" can only be asked once
	 * and this is the first place that can ask it.
	 *
	 * Read before anything else claims a peripheral, and latched rather than
	 * re-read later: by the time the boot task runs, an operator who let go
	 * of the button would look like one who never pressed it.
	 *
	 * The pin has the module's pull-up and the switch shorts it to ground, so
	 * pressed reads low. The delay is for the pull-up to charge the pin
	 * capacitance after the input buffer is enabled -- without it the first
	 * read can return the floating level, which is a factory reset nobody
	 * asked for on the Zephyr image and a provisioning console nobody asked
	 * for here.
	 */
	nrf_gpio_cfg_input(WOZ_FREERTOS_PIN_SW2, NRF_GPIO_PIN_PULLUP);
	woz_freertos_busy_wait_us(100);
	s_prov_mode = (nrf_gpio_pin_read(WOZ_FREERTOS_PIN_SW2) == 0u);

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
