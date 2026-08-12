/*
 * The UWB side of the BSP: the vector the board routes to the DW3110's
 * interrupt line.
 *
 * The rest of the DW3110 surface is modules/woz_dw3000's dw3000_hw.h and
 * dw3000_spi.h, which this port implements rather than extends. Only the vector
 * entry point belongs here, because only the board's vector table needs it.
 */
#ifndef WOZ_FREERTOS_UWB_H
#define WOZ_FREERTOS_UWB_H

/*
 * GPIOTE_IRQHandler. It runs at priority 4 and calls a FreeRTOS FromISR API, so
 * it must stay at or below configMAX_SYSCALL_INTERRUPT_PRIORITY; the source
 * asserts that relation at compile time, because on this kernel the runtime
 * check cannot fire.
 */
void woz_freertos_dw3000_irq_handler(void);

#endif /* WOZ_FREERTOS_UWB_H */
