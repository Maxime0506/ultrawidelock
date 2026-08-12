/*
 * DWM3001CDK pin map for the DW3110, taken from the pinned Qorvo SDK.
 *
 * Every number here is copied from
 * Projects/FreeRTOS/QANI/DWM3001CDK/ProjectDefinition/uwb_stack_llhw.cmake in
 * DW3_QM33_SDK_1.1.1.zip, the archive make freertos-platform-check pins by
 * SHA-256. The module is a sealed package with the nRF52833 and the DW3110 on
 * one substrate, so these are not board-design choices anyone may revisit --
 * they are what the traces do.
 *
 * CS is not SPIM's own CSN. The DW3110 wakes on a 500 us CS-low pulse with no
 * clock on the bus, which hardware chip-select cannot produce because it only
 * asserts around a transfer. So CS is an ordinary GPIO the port drives, exactly
 * as the Zephyr and ESP-IDF backends do.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_UWB_BOARD_PINS_H
#define ULTRAWIDELOCK_FREERTOS_UWB_BOARD_PINS_H

/* Port 0 pins are 0-31, port 1 pins are 32-63: the part's own flat numbering. */
#define ULTRAWIDELOCK_DW3000_PIN_SCLK   3u  /* P0.03 */
#define ULTRAWIDELOCK_DW3000_PIN_MOSI   8u  /* P0.08 */
#define ULTRAWIDELOCK_DW3000_PIN_MISO   29u /* P0.29 */
#define ULTRAWIDELOCK_DW3000_PIN_CS     38u /* P1.06 */
#define ULTRAWIDELOCK_DW3000_PIN_IRQ    34u /* P1.02 */
#define ULTRAWIDELOCK_DW3000_PIN_RST    25u /* P0.25 */
#define ULTRAWIDELOCK_DW3000_PIN_WAKEUP 51u /* P1.19 */

/*
 * Two clocks, as every DW3000 port has: the chip only accepts SPI below 7 MHz
 * until its PLL has locked, and the decadriver calls setslowrate/setfastrate
 * around that. Fast is 8 MHz, which is what the Zephyr oracle's DWM3001CDK
 * overlay asks for on this same silicon; the frame-pull path is dominated by
 * per-transfer overhead rather than bit time, so a higher clock buys little.
 */
#define ULTRAWIDELOCK_DW3000_SPI_SLOW_HZ 2000000u
#define ULTRAWIDELOCK_DW3000_SPI_FAST_HZ 8000000u

/*
 * The largest single DW3110 transaction this product performs: a 1023-byte
 * frame, a two-byte header and a CRC byte, rounded up to a word. Nothing here
 * reads the CIR accumulator, which is the only access that would be larger.
 * A request past this is refused rather than truncated -- a short SPI transfer
 * to this chip does not fail, it returns a different register.
 */
#define ULTRAWIDELOCK_DW3000_SPI_XFER_MAX 1028u

/*
 * The GPIOTE channel the DW3110 interrupt line takes. Here rather than beside
 * its only user in dw3000_hw_freertos.c because it is a peripheral claim, and
 * the layer that can check it against the other stacks' claims is the 802.15.4
 * one: see radio/peripheral_asserts_freertos.c, which is the only other reader.
 */
#ifndef ULTRAWIDELOCK_DW3000_GPIOTE_CHANNEL
#define ULTRAWIDELOCK_DW3000_GPIOTE_CHANNEL 0u
#endif

#endif /* ULTRAWIDELOCK_FREERTOS_UWB_BOARD_PINS_H */
