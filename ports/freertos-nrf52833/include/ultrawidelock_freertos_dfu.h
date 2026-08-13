/*
 * The over-the-air update channel's bring-up.
 *
 * Everything else about updates is portable and lives elsewhere:
 * modules/ultrawidelock_dfu/include/ultrawidelock_dfu_rx.h is the receiver the transport feeds, and
 * the applier runs in MCUboot. What is here is the one call that puts this
 * port's transport in front of it.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_DFU_H
#define ULTRAWIDELOCK_FREERTOS_DFU_H

/**
 * Register the update channel and arm the button that opens its window.
 *
 * Call before ultrawidelock_freertos_nimble_host_start() -- the same ordering rule as
 * every other NimBLE registrant, because the services are added inside that
 * sequence. In practice that means before ultrawidelock_reader_start(), which is what
 * starts the host on this port.
 *
 * Returns 0, or -1 if the hook table is full. A button that will not configure
 * is a warning rather than a failure: the window can still be opened in
 * software, and the update path is the thing that fixes a broken board.
 *
 * Returning 0 does not mean an update can be pushed. Nothing is accepted until
 * ultrawidelock_dfu_window_open() has been called, which SW2 does.
 */
int dfu_ble_start(void);

#endif /* ULTRAWIDELOCK_FREERTOS_DFU_H */
