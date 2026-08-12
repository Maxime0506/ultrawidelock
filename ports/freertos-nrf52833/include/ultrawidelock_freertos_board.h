/*
 * Board interrupt entry points that are not the radio's.
 *
 * The radio's own vectors are in ultrawidelock_freertos_radio.h; this header carries what
 * the rest of the BSP needs the board's vector table to route.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_BOARD_H
#define ULTRAWIDELOCK_FREERTOS_BOARD_H

/*
 * RNG. Route the board's RNG vector here at a priority below MPSL and the
 * 802.15.4 driver: the handler only fills this port's own pool and calls
 * nothing, so it can sit anywhere lower without a FreeRTOS constraint.
 * peripherals.yml freezes the choice.
 */
void ultrawidelock_freertos_rng_isr(void);

#endif /* ULTRAWIDELOCK_FREERTOS_BOARD_H */
