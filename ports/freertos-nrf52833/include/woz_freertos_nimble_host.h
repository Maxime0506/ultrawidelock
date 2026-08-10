/*
 * Startup for the Apache NimBLE host on FreeRTOS.
 *
 * This sits one layer above woz_freertos_radio.h: the radio sequencer brings
 * up MPSL and the SoftDevice Controller and publishes the HCI transport, and
 * this brings up the host that talks across it. Product code should call only
 * woz_freertos_nimble_host_start(), which does both in order.
 */
#ifndef WOZ_FREERTOS_NIMBLE_HOST_H
#define WOZ_FREERTOS_NIMBLE_HOST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Only two steps of the sequence can fail. nimble_port_init() and
 * ble_hs_sched_start() both return void: the porting layer reports allocation
 * failures through its own assertions, and scheduling startup only enqueues an
 * event. Synchronization failures surface later through the synced flag, not
 * from start.
 */
enum woz_freertos_nimble_host_stage {
	WOZ_FREERTOS_NIMBLE_HOST_STAGE_RADIO = 1,
	WOZ_FREERTOS_NIMBLE_HOST_STAGE_TASK,
};

/**
 * Bring up the controller and then the NimBLE host, and schedule host/
 * controller synchronization.
 *
 * Returns zero on success, or the negative
 * @ref woz_freertos_nimble_host_stage that failed. Call it once, from a task.
 *
 * Returning zero means the host task is running and the synchronization
 * sequence is queued, not that the link is usable yet. Wait for
 * woz_freertos_nimble_host_synced() before advertising.
 */
int woz_freertos_nimble_host_start(void);

/** True once the host task exists and startup has been scheduled. */
bool woz_freertos_nimble_host_ready(void);

/**
 * True once the host and controller have completed the HCI synchronization
 * sequence. Goes false again if the host resets itself.
 */
bool woz_freertos_nimble_host_synced(void);

/** Count of host-initiated stack resets since start, for diagnostics. */
uint32_t woz_freertos_nimble_host_resets(void);

#endif /* WOZ_FREERTOS_NIMBLE_HOST_H */
