/*
 * FreeRTOS ownership for the Apache NimBLE host to Nordic SoftDevice
 * Controller HCI boundary.
 *
 * The board layer supplies Nordic's opcode dispatcher, ACL input, and message
 * output functions. Keeping those calls behind this table lets the transport
 * stay independent of Zephyr and makes its buffer ownership testable on host.
 */
#ifndef WOZ_FREERTOS_NIMBLE_SDC_H
#define WOZ_FREERTOS_NIMBLE_SDC_H

#include <stdint.h>

enum woz_freertos_nimble_sdc_fault {
	WOZ_NIMBLE_SDC_FAULT_GET = 1,
	WOZ_NIMBLE_SDC_FAULT_PACKET,
	WOZ_NIMBLE_SDC_FAULT_HOST,
};

struct woz_freertos_nimble_sdc_ops {
	int32_t (*cmd_put)(uint8_t *packet);
	int32_t (*data_put)(const uint8_t *packet);
	int32_t (*msg_get)(uint8_t *packet, uint8_t *type);
	void (*fault)(enum woz_freertos_nimble_sdc_fault fault, int32_t detail);
	int32_t no_data_error;
};

/**
 * Configure the controller entry points before NimBLE calls
 * ble_transport_init(). The table is copied and may be released afterward.
 */
int woz_freertos_nimble_sdc_configure(const struct woz_freertos_nimble_sdc_ops *ops);

/** Wake the HCI receive task after controller output becomes available. */
void woz_freertos_nimble_sdc_wake(void);

/** ISR-safe wake used by the MPSL low-priority/controller signal path. */
void woz_freertos_nimble_sdc_wake_from_isr(void);

#endif /* WOZ_FREERTOS_NIMBLE_SDC_H */
