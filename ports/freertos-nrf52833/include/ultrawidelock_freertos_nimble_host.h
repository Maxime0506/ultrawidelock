/*
 * Startup for the Apache NimBLE host on FreeRTOS.
 *
 * This sits one layer above ultrawidelock_freertos_radio.h: the radio sequencer brings
 * up MPSL and the SoftDevice Controller and publishes the HCI transport, and
 * this brings up the host that talks across it. Product code should call only
 * ultrawidelock_freertos_nimble_host_start(), which does both in order.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_H
#define ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Only two steps of the sequence can fail. nimble_port_init() and
 * ble_hs_sched_start() both return void: the porting layer reports allocation
 * failures through its own assertions, and scheduling startup only enqueues an
 * event. Synchronization failures surface later through the synced flag, not
 * from start.
 */
enum ultrawidelock_freertos_nimble_host_stage {
	ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_STAGE_RADIO = 1,
	ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_STAGE_TASK,
	ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_STAGE_SERVICES,
};

/**
 * Product hooks into the startup sequence.
 *
 * Both exist because GATT registration has to land inside the sequence, not
 * before or after it: NimBLE's service tables need the memory pools that
 * nimble_port_init() creates, and they must be registered before the host task
 * begins processing events. A product that registered them from its own task
 * would be racing the sync it is registering for.
 *
 * Either member may be NULL; a registrant that only adds services does not
 * have to care about sync.
 */
struct ultrawidelock_freertos_nimble_host_hooks {
	/** Runs after nimble_port_init(), before the host task is created.
	 *  Nonzero aborts startup at STAGE_SERVICES. */
	int (*register_services)(void);
	/** Runs on the host task after the port's own sync bookkeeping. */
	void (*on_sync)(void);
};

/**
 * More than one, because this image has more than one BLE service.
 *
 * The credential reader and the update channel each own a GATT service and an L2CAP
 * CoC server, and neither knows the other exists -- the reader is shared with
 * the ESP32 port and the update channel is shared with the Zephyr one. Making
 * one call the other would couple two layers that have no reason to meet.
 *
 * Three since the Matter commissioning transport joined them: it owns the
 * 0xFFF6 service and observes GAP through a listener rather than the connection
 * callback, for the same reason -- the reader owns the advertising set and
 * therefore that callback. A build without ULTRAWIDELOCK_MATTER simply never adds the
 * third, so the slot costs one pointer pair it does not use.
 */
#define ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_HOOKS_MAX 3u

/**
 * Add a registrant to the startup sequence.
 *
 * Call before ultrawidelock_freertos_nimble_host_start(); afterwards has no effect on a
 * sequence that already ran. Copied by value, so @p hooks may be a stack local.
 *
 * Registrants run in the order they were added, which is the order their
 * services appear in the attribute table -- so a product that cares about
 * handle numbers controls them by ordering these calls.
 *
 * Returns 0, or -1 if @p hooks is NULL or the table is full. Passing NULL is
 * not how the table is cleared; nothing clears it, because a BLE service is
 * registered once per boot.
 */
int ultrawidelock_freertos_nimble_host_add_hooks(const struct ultrawidelock_freertos_nimble_host_hooks *hooks);

/**
 * Bring up the controller and then the NimBLE host, and schedule host/
 * controller synchronization.
 *
 * Returns zero on success, or the negative
 * @ref ultrawidelock_freertos_nimble_host_stage that failed. Call it once, from a task.
 *
 * Returning zero means the host task is running and the synchronization
 * sequence is queued, not that the link is usable yet. Wait for
 * ultrawidelock_freertos_nimble_host_synced() before advertising.
 */
int ultrawidelock_freertos_nimble_host_start(void);

/** True once the host task exists and startup has been scheduled. */
bool ultrawidelock_freertos_nimble_host_ready(void);

/**
 * True once the host and controller have completed the HCI synchronization
 * sequence. Goes false again if the host resets itself.
 */
bool ultrawidelock_freertos_nimble_host_synced(void);

/** Count of host-initiated stack resets since start, for diagnostics. */
uint32_t ultrawidelock_freertos_nimble_host_resets(void);

#endif /* ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_H */
