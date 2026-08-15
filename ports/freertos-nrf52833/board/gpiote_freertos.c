/* SPDX-License-Identifier: ISC */

/*
 * The GPIOTE vector, and the only thing that owns it.
 *
 * GPIOTE is the one peripheral on this board with more than one legitimate
 * user. peripherals.yml names it `dw3000_irq` because that is the claim that
 * matters -- the DW3110 interrupt line on P1.02 at priority 4, with a
 * ~1.836 ms response-arm deadline behind it -- but a second edge-triggered
 * input costs a channel, not a peripheral, and the update button is one.
 * A vector can only have one definition, so the arbitration has to live
 * somewhere. It lives here.
 *
 * WHAT THIS WAS FIXING. Until this file existed, nothing defined
 * GPIOTE_IRQHandler at all: startup_freertos.c declares it weak, aliased to
 * default_handler, which spins forever. Every claim in peripherals.yml was
 * satisfied except the routing, so the first DW3110 edge would have parked the
 * core in that loop with the interrupt still latched. The dw3000 layer already
 * enabled the channel and the NVIC line; it just had no way to be called.
 *
 * DELIBERATELY DUMB. Each registered handler checks its own event and clears
 * it, exactly as dw3000_hw_freertos.c already did on its own -- this does not
 * decode the event register and dispatch by channel. Two reasons. The channel a
 * handler owns is its own business and would have to be told to this file
 * anyway, and the fan-out is two calls on a part where the IN event registers
 * are adjacent words: decoding costs more than calling. The cost is that a
 * handler which forgets to check its event will see every other handler's
 * edges, which is a bug in that handler and shows up on the first press.
 *
 * PRIORITY IS NOT SET HERE. dw3000_hw_init_interrupt() sets GPIOTE_IRQn to
 * ULTRAWIDELOCK_DW3000_IRQ_PRIORITY, and everything else on this vector inherits it.
 * That is the right way round: the DW3110 line is the one with a deadline, and
 * a second user is not entitled to move it. A handler registered here therefore
 * runs at priority 4 and is bound by the FreeRTOS syscall ceiling like any
 * other -- it may call the FromISR APIs and nothing else.
 */
#include "ultrawidelock_freertos_board.h"

#include <stddef.h>

#include <nrfx.h>

/*
 * Two: the DW3110 interrupt line and the update button. A third user would be a
 * design decision rather than an array size, which is why this is a fixed table
 * and a full one is a build-time failure at its call site rather than a
 * silently dropped registration.
 */
#define ULTRAWIDELOCK_FREERTOS_GPIOTE_HANDLERS 2u

static ultrawidelock_freertos_gpiote_handler s_handlers[ULTRAWIDELOCK_FREERTOS_GPIOTE_HANDLERS];

int ultrawidelock_freertos_gpiote_add_handler(ultrawidelock_freertos_gpiote_handler fn)
{
	if (fn == NULL) {
		return -1;
	}
	for (size_t i = 0; i < ULTRAWIDELOCK_FREERTOS_GPIOTE_HANDLERS; i++) {
		if (s_handlers[i] == fn) {
			/* Idempotent: re-initialising a driver must not double
			 * its dispatch, and a handler that ran twice per edge
			 * would look like a duplicated frame, not like this. */
			return 0;
		}
		if (s_handlers[i] == NULL) {
			s_handlers[i] = fn;
			return 0;
		}
	}
	return -1;
}

void GPIOTE_IRQHandler(void)
{
	for (size_t i = 0; i < ULTRAWIDELOCK_FREERTOS_GPIOTE_HANDLERS; i++) {
		if (s_handlers[i] != NULL) {
			s_handlers[i]();
		}
	}
}
