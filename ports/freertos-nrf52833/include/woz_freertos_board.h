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

#endif /* WOZ_FREERTOS_BOARD_H */
