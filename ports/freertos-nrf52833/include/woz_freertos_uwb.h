/*
 * The UWB side of the BSP: the vector the board routes to the DW3110's
 * interrupt line, and the port's bring-up of the part behind it.
 *
 * The rest of the DW3110 surface is modules/woz_dw3000's dw3000_hw.h and
 * dw3000_spi.h, which this port implements rather than extends. Only the vector
 * entry point and the bring-up belong here, because only the board's vector
 * table and the application need them.
 */
#ifndef WOZ_FREERTOS_UWB_H
#define WOZ_FREERTOS_UWB_H

#include <stdbool.h>

/*
 * GPIOTE_IRQHandler. It runs at priority 4 and calls a FreeRTOS FromISR API, so
 * it must stay at or below configMAX_SYSCALL_INTERRUPT_PRIORITY; the source
 * asserts that relation at compile time, because on this kernel the runtime
 * check cannot fire.
 */
void woz_freertos_dw3000_irq_handler(void);

/*
 * Bring the DW3110 up and pre-apply the expected session PHY.
 *
 * Everything under uwb/ is reached from the ranging engine, which the Aliro
 * seam drives from the BLE M1-M4 handshake. Until that seam is wired, nothing
 * in the image calls the layer at all: --gc-sections drops it, the flash figure
 * says nothing about it, and no line of this port's SPI, reset, wake or
 * interrupt code has ever run on the part it was written for. This is what
 * closes that gap.
 *
 * The PHY applied is the one an Aliro session is going to negotiate anyway, so
 * this is a real step rather than a probe invented for the occasion: it walks
 * the whole backend -- SPI transfers, the reset sequence, the device-ID read,
 * the interrupt line -- and leaves the radio configured with RX unarmed, which
 * is the state the M4-time start wants to find.
 *
 * Call from a task, not from main(): the interrupt path notifies a worker task,
 * and the SPI transfers wait on the peripheral rather than spinning through a
 * scheduler that is not running yet.
 *
 * Returns 0 when the part answered and the PHY was applied, or the engine's
 * negative error otherwise. A failure is worth logging and is not worth
 * halting on: the other unlock paths do not go through this radio, and a lock
 * that refuses to boot cannot tell anyone why its UWB is broken.
 */
int woz_freertos_uwb_start(void);

/* True once woz_freertos_uwb_start() has succeeded. For status output that
 * would otherwise have to guess, and so the boot log says something
 * falsifiable about the radio rather than that it was asked to start. */
bool woz_freertos_uwb_ready(void);

/*
 * True while a UWB ranging session is listening. The flash driver defers NVMC
 * work behind it: MPSL grants flash timeslots against BLE and 802.15.4 only,
 * the UWB slot grid is invisible to it, and NVMC stalls the CPU -- so an
 * unlucky grant lands a multi-millisecond stall inside the ~1,836 us window
 * between a DW3110 frame and its armed response. The strong definition lives
 * with the UWB bring-up; board/flash_freertos.c carries a weak false default
 * so the host tests and a UWB-less image link unchanged.
 */
bool woz_freertos_uwb_ranging_active(void);

#endif /* WOZ_FREERTOS_UWB_H */
