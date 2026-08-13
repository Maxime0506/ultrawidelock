/*
 * Startup order for the shared nRF52833 radio: MPSL first, then the
 * SoftDevice Controller, then the NimBLE HCI transport contract.
 *
 * The board owns the vector table; this sequencer owns the order, the
 * interrupt priorities frozen in peripherals.yml, and the static controller
 * memory pool.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_RADIO_H
#define ULTRAWIDELOCK_FREERTOS_RADIO_H

#include <stdbool.h>
#include <stdint.h>

enum ultrawidelock_freertos_radio_stage {
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_MPSL_INIT = 1,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_MPSL_WORKER,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_INIT,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_SUPPORT,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_CFG,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_MEMORY,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_RAND,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_ENABLE,
	ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_TRANSPORT,
};

/*
 * The controller's command API is opcode-specific, so an HCI command has to be
 * decoded before it reaches the controller and the resulting Command Complete
 * or Command Status has to be read back out of the decoder. Both halves belong
 * to one dispatcher: msg_get must drain the event the dispatcher generated for
 * the last command before it asks the controller for anything new.
 */
struct ultrawidelock_freertos_radio_dispatcher {
	/** Consume one HCI command packet. Returns a negative nrf_errno on failure. */
	int32_t (*cmd_put)(uint8_t *packet);
	/** Retrieve one HCI event or data packet, and its SDC message type. */
	int32_t (*msg_get)(uint8_t *packet, uint8_t *type);
};

/**
 * The pinned Nordic opcode dispatcher, implemented in
 * ble/hci_dispatcher_freertos.c. Never NULL.
 */
const struct ultrawidelock_freertos_radio_dispatcher *ultrawidelock_freertos_radio_sdc_dispatcher(void);

/**
 * Bring up MPSL and the SoftDevice Controller, then publish the HCI transport
 * contract used by NimBLE.
 *
 * Returns zero on success, or the negative @ref ultrawidelock_freertos_radio_stage that
 * failed. Call it once, before ble_transport_init().
 *
 * It must be called before vTaskStartScheduler(), not from a task, and that is
 * a hard ordering rather than a preference. MPSL owns CLOCK and starts the
 * low-frequency crystal inside mpsl_init(); RTC1 carries the FreeRTOS tick and
 * counts from that same crystal. Starting the scheduler first would start it on
 * a clock that is not running, which board/tick_freertos.c reports as a fatal
 * rather than a hang, but which is still a board that never boots.
 *
 * Nothing here blocks on the scheduler. The MPSL worker task is created
 * statically and the notifications its low-priority handler posts before the
 * scheduler starts are latched and delivered when it does.
 */
int ultrawidelock_freertos_radio_start(const struct ultrawidelock_freertos_radio_dispatcher *dispatcher);

/** True after the controller is enabled and the transport contract is published. */
bool ultrawidelock_freertos_radio_ready(void);

/** Bytes of static controller memory the last successful start consumed. */
uint32_t ultrawidelock_freertos_radio_memory_used(void);

/*
 * Interrupt entry points the board's vector table must route. RADIO, RTC0,
 * TIMER0, and POWER_CLOCK belong to MPSL at priority 0; SWI5_EGU5 is the MPSL
 * low-priority signal and must sit at a FreeRTOS-callable priority.
 */
void ultrawidelock_freertos_radio_radio_isr(void);
void ultrawidelock_freertos_radio_rtc0_isr(void);
void ultrawidelock_freertos_radio_timer0_isr(void);
void ultrawidelock_freertos_radio_power_clock_isr(void);

/**
 * Watch the POWER half of the POWER_CLOCK vector, after MPSL has had it.
 *
 * MPSL owns this vector and the CLOCK events on it, but the POWER peripheral's
 * USB supply events -- USBDETECTED, USBREMOVED, USBPWRRDY -- arrive on the same
 * line and MPSL neither reads nor clears them. The USB stack cannot install its
 * own handler here, so it registers one and this port calls it.
 *
 * A handler runs at MPSL's priority, which is 0. That is ABOVE the FreeRTOS
 * syscall ceiling, so it may not call any FreeRTOS API, FromISR or otherwise.
 * It must check its own POWER events and clear them.
 *
 * Returns 0, or -1 if @p fn is NULL or the single slot is taken. One slot,
 * because USB is the only thing on this board with a reason to watch POWER.
 */
typedef void (*ultrawidelock_freertos_power_handler)(void);
int ultrawidelock_freertos_radio_set_power_handler(ultrawidelock_freertos_power_handler fn);
void ultrawidelock_freertos_radio_low_priority_isr(void);

#endif /* ULTRAWIDELOCK_FREERTOS_RADIO_H */
