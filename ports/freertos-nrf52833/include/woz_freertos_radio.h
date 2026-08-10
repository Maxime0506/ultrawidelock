/*
 * Startup order for the shared nRF52833 radio: MPSL first, then the
 * SoftDevice Controller, then the NimBLE HCI transport contract.
 *
 * The board owns the vector table; this sequencer owns the order, the
 * interrupt priorities frozen in peripherals.yml, and the static controller
 * memory pool.
 */
#ifndef WOZ_FREERTOS_RADIO_H
#define WOZ_FREERTOS_RADIO_H

#include <stdbool.h>
#include <stdint.h>

enum woz_freertos_radio_stage {
	WOZ_FREERTOS_RADIO_STAGE_MPSL_INIT = 1,
	WOZ_FREERTOS_RADIO_STAGE_MPSL_WORKER,
	WOZ_FREERTOS_RADIO_STAGE_SDC_INIT,
	WOZ_FREERTOS_RADIO_STAGE_SDC_SUPPORT,
	WOZ_FREERTOS_RADIO_STAGE_SDC_CFG,
	WOZ_FREERTOS_RADIO_STAGE_SDC_MEMORY,
	WOZ_FREERTOS_RADIO_STAGE_SDC_RAND,
	WOZ_FREERTOS_RADIO_STAGE_SDC_ENABLE,
	WOZ_FREERTOS_RADIO_STAGE_TRANSPORT,
};

/*
 * The controller's command API is opcode-specific, so an HCI command has to be
 * decoded before it reaches the controller and the resulting Command Complete
 * or Command Status has to be read back out of the decoder. Both halves belong
 * to one dispatcher: msg_get must drain the event the dispatcher generated for
 * the last command before it asks the controller for anything new.
 */
struct woz_freertos_radio_dispatcher {
	/** Consume one HCI command packet. Returns a negative nrf_errno on failure. */
	int32_t (*cmd_put)(uint8_t *packet);
	/** Retrieve one HCI event or data packet, and its SDC message type. */
	int32_t (*msg_get)(uint8_t *packet, uint8_t *type);
};

/**
 * The pinned Nordic opcode dispatcher, implemented in
 * ble/hci_dispatcher_freertos.c. Never NULL.
 */
const struct woz_freertos_radio_dispatcher *woz_freertos_radio_sdc_dispatcher(void);

/**
 * Bring up MPSL and the SoftDevice Controller, then publish the HCI transport
 * contract used by NimBLE.
 *
 * Returns zero on success, or the negative @ref woz_freertos_radio_stage that
 * failed. Call it once, from a task, before ble_transport_init().
 */
int woz_freertos_radio_start(const struct woz_freertos_radio_dispatcher *dispatcher);

/** True after the controller is enabled and the transport contract is published. */
bool woz_freertos_radio_ready(void);

/** Bytes of static controller memory the last successful start consumed. */
uint32_t woz_freertos_radio_memory_used(void);

/*
 * Interrupt entry points the board's vector table must route. RADIO, RTC0,
 * TIMER0, and POWER_CLOCK belong to MPSL at priority 0; SWI5_EGU5 is the MPSL
 * low-priority signal and must sit at a FreeRTOS-callable priority.
 */
void woz_freertos_radio_radio_isr(void);
void woz_freertos_radio_rtc0_isr(void);
void woz_freertos_radio_timer0_isr(void);
void woz_freertos_radio_power_clock_isr(void);
void woz_freertos_radio_low_priority_isr(void);

#endif /* WOZ_FREERTOS_RADIO_H */
