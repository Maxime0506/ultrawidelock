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

#endif /* WOZ_FREERTOS_BOARD_H */
