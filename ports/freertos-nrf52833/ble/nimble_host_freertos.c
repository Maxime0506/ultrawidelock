#include "woz_freertos_nimble_host.h"

#include <stddef.h>

#include "woz_freertos_platform.h"
#include "woz_freertos_radio.h"

#include "FreeRTOS.h"
#include "task.h"

#include <host/ble_hs.h>
#include <nimble/nimble_port.h>

#if configSUPPORT_STATIC_ALLOCATION != 1
#error "The NimBLE host task requires configSUPPORT_STATIC_ALLOCATION=1"
#endif
/*
 * NimBLE's FreeRTOS porting layer creates its own kernel objects with the
 * dynamic FreeRTOS APIs: one event queue (xQueueCreate), the host and HCI
 * mutexes and the GAP preemption mutex (xSemaphoreCreateRecursiveMutex), the
 * HCI acknowledgement semaphore (xSemaphoreCreateCounting), and one software
 * timer per callout (xTimerCreate). Patching the vendor tree to avoid that is
 * not an option here, so the heap has to exist. Every one of those allocations
 * happens once during nimble_port_init() and none is ever freed, so the heap
 * is a fixed startup cost rather than a source of runtime fragmentation.
 * Everything this port itself owns is still statically allocated.
 */
#if configSUPPORT_DYNAMIC_ALLOCATION != 1
#error "The NimBLE porting layer requires configSUPPORT_DYNAMIC_ALLOCATION=1"
#endif
#if configUSE_TIMERS != 1
#error "NimBLE callouts are FreeRTOS software timers; set configUSE_TIMERS=1"
#endif

#ifndef WOZ_FREERTOS_NIMBLE_HOST_STACK_BYTES
#define WOZ_FREERTOS_NIMBLE_HOST_STACK_BYTES 4096u
#endif

/*
 * Below the HCI receive pump and the MPSL worker, above ordinary application
 * work: the host must not delay the controller, but it must still drain its
 * event queue ahead of product logic.
 */
#ifndef WOZ_FREERTOS_NIMBLE_HOST_TASK_PRIORITY
#define WOZ_FREERTOS_NIMBLE_HOST_TASK_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#define HOST_STACK_WORDS                                                                   \
	((WOZ_FREERTOS_NIMBLE_HOST_STACK_BYTES + sizeof(StackType_t) - 1u) /               \
	 sizeof(StackType_t))

#define WOZ_FREERTOS_NIMBLE_HOST_TAG "ble_host"

static TaskHandle_t s_task;
static StaticTask_t s_task_storage;
static StackType_t s_stack[HOST_STACK_WORDS];
static bool s_ready;
static volatile bool s_synced;
static volatile uint32_t s_resets;
static struct woz_freertos_nimble_host_hooks s_hooks;

void woz_freertos_nimble_host_set_hooks(const struct woz_freertos_nimble_host_hooks *hooks)
{
	if (hooks == NULL) {
		s_hooks.register_services = NULL;
		s_hooks.on_sync = NULL;
		return;
	}
	s_hooks = *hooks;
}

bool woz_freertos_nimble_host_ready(void)
{
	return s_ready;
}

bool woz_freertos_nimble_host_synced(void)
{
	return s_synced;
}

uint32_t woz_freertos_nimble_host_resets(void)
{
	return s_resets;
}

/* Runs on the host task once the controller has answered the startup sequence. */
static void on_sync(void)
{
	s_synced = true;
	woz_freertos_log(WOZ_FREERTOS_LOG_INFO, WOZ_FREERTOS_NIMBLE_HOST_TAG,
			 "host synced with the controller");
	/* After the flag, so a hook that advertises sees a synced host. */
	if (s_hooks.on_sync != NULL) {
		s_hooks.on_sync();
	}
}

/*
 * The host resets itself on a fatal HCI error and then resynchronizes on its
 * own, so this is reported rather than fatal. Anything the product had
 * advertised or connected is gone, which is why the synced flag drops.
 */
static void on_reset(int reason)
{
	s_synced = false;
	s_resets++;
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_NIMBLE_HOST_TAG,
			 "host reset, reason %d", reason);
}

static void host_task(void *arg)
{
	(void)arg;
	/* Never returns: it is the default event queue's only consumer. */
	nimble_port_run();
}

int woz_freertos_nimble_host_start(void)
{
	int rc;

	if (s_ready) {
		return 0;
	}

	rc = woz_freertos_radio_start(woz_freertos_radio_sdc_dispatcher());
	if (rc != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_NIMBLE_HOST_TAG,
				 "radio startup failed at stage %d", -rc);
		return -WOZ_FREERTOS_NIMBLE_HOST_STAGE_RADIO;
	}

	/*
	 * Initializes the NPL default queue and memory pools, then the transport
	 * and the host. The transport half of this reaches back into
	 * ble/nimble_sdc_transport.c, which is why the radio has to be up first:
	 * its ops table is published by woz_freertos_radio_start().
	 */
	nimble_port_init();

	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	/*
	 * Between the pools existing and the host task consuming events: service
	 * tables need the former and must be registered before the latter.
	 */
	if (s_hooks.register_services != NULL) {
		rc = s_hooks.register_services();
		if (rc != 0) {
			woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_NIMBLE_HOST_TAG,
					 "service registration failed rc=%d", rc);
			return -WOZ_FREERTOS_NIMBLE_HOST_STAGE_SERVICES;
		}
	}

	s_task = xTaskCreateStatic(host_task, "ble_host", (uint32_t)HOST_STACK_WORDS, NULL,
				   WOZ_FREERTOS_NIMBLE_HOST_TASK_PRIORITY, s_stack,
				   &s_task_storage);
	if (s_task == NULL) {
		return -WOZ_FREERTOS_NIMBLE_HOST_STAGE_TASK;
	}

	/*
	 * Queues the synchronization sequence onto the default event queue
	 * instead of running it here, so this is safe to call from any task and
	 * the HCI exchange happens on the host task that owns the queue.
	 */
	ble_hs_sched_start();

	s_ready = true;
	return 0;
}
