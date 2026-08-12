/*
 * Board interrupt entry points that are not the radio's.
 *
 * The radio's own vectors are in woz_freertos_radio.h; this header carries what
 * the rest of the BSP needs the board's vector table to route.
 */
#ifndef WOZ_FREERTOS_BOARD_H
#define WOZ_FREERTOS_BOARD_H

/*
 * RNG. Route the board's RNG vector here at a priority below MPSL and the
 * 802.15.4 driver: the handler only fills this port's own pool and calls
 * nothing, so it can sit anywhere lower without a FreeRTOS constraint.
 * peripherals.yml freezes the choice.
 */
void woz_freertos_rng_isr(void);

/*
 * GPIOTE. One vector, more than one legitimate user: the DW3110 interrupt line
 * and the update button each take a channel. board/gpiote_freertos.c owns the
 * vector and fans it out; see that file for why the fan-out is unconditional.
 *
 * A handler runs at whatever priority the DW3110 line set (4), so it is bound
 * by the FreeRTOS syscall ceiling: the FromISR APIs and nothing else. It must
 * check its own GPIOTE event before acting and clear it, because it will be
 * called for every edge on the peripheral, not only its own.
 */
typedef void (*woz_freertos_gpiote_handler)(void);

/**
 * Register @p fn to be called from GPIOTE_IRQHandler.
 *
 * Idempotent for a handler already registered. Returns 0, or -1 if @p fn is
 * NULL or the table is full.
 */
int woz_freertos_gpiote_add_handler(woz_freertos_gpiote_handler fn);

/*
 * SW2, the DWM3001CDK's only usable push button.
 *
 * P0.02, with the module's pull-up and active low -- byte for byte the Zephyr
 * oracle's `button2` node, which is the sw0 alias every routine on that side
 * reaches it through. SW1 is on P0.18, which is nRESET by default and resets
 * the board, so there is no second button to hand out.
 *
 * Three routines want it and they do not conflict, because two of them only
 * look at boot: held through reset it means provisioning mode or factory reset,
 * and pressed while the application is running it opens an update window.
 *
 * The channel is 1 because 0 is the DW3110 interrupt line; the two are asserted
 * disjoint, and against the 802.15.4 driver's debug pins, in
 * radio/peripheral_asserts_freertos.c.
 */
#define WOZ_FREERTOS_PIN_SW2 2u
#define WOZ_FREERTOS_SW2_GPIOTE_CHANNEL 1u

#endif /* WOZ_FREERTOS_BOARD_H */
