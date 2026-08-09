/**
 * @file cirdiag_capture.c — unattended CIR capture cycle for the DWM3001CDK.
 *
 * Arm the CIR dump, hold it for a window, disarm so the ring drains, wait,
 * repeat -- forever, from boot. Not a shell command because this board has no
 * console input, and the one button is already the factory reset. The operator
 * watches `make monitor`: each cycle prints `cir.cycle: n=<i>` markers around a
 * labelled interval; walk up from outside during odd cycles, inside during
 * even, and the labelling problem is solved.
 *
 * With CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOWS the windowed-CIR dump is armed --
 * which on this board stops the responder transmitting entirely (measured: tx0,
 * no range). Without it only the summary path runs (one `[ALAB] ev=uwb.diag`
 * line per reception), and that is the mode worth using: the tap-derived half
 * of the feature set is worth only 0.14 accuracy points. The markers carry no
 * [ALAB] prefix so the parser ignores them.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "uwb_cirdiag.h"
#include "woz_log.h"

LOG_MODULE_DECLARE(main, CONFIG_LOG_DEFAULT_LEVEL);

/**
 * Capture loop: arm the dump, hold it open, disarm to drain the ring, idle, repeat forever.
 * Never returns.
 */
static void cirdiag_capture_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Nothing here needs the radio to be up: uwb_cirdiag_set_enabled() and
	 * uwb_cirdiag_dump_set_enabled() are documented safe before the chip is
	 * probed, because the chip-side CIA logging enable happens lazily on the
	 * first armed reception. Waiting for a ranging session would mean knowing
	 * about one, which this file does not. */
#if defined(CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOWS)
	LOG_WRN("CIR capture image: %d ms armed, %d ms idle. NOT a shipping build.",
		CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOW_MS, CONFIG_ALIRO_CIRDIAG_CAPTURE_IDLE_MS);

	for (uint32_t n = 1;; n++) {
		woz_printf("cir.cycle: n=%u armed\n", (unsigned)n);
		uwb_cirdiag_dump_set_enabled(true);

		k_msleep(CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOW_MS);

		/* Read the count before disarming: the disarm IS the drain, and it
		 * empties the ring on its way out. */
		const uint32_t recs = uwb_cirdiag_ring_count();

		uwb_cirdiag_dump_set_enabled(false);
		woz_printf("cir.cycle: n=%u drained recs=%u\n", (unsigned)n, (unsigned)recs);

		k_msleep(CONFIG_ALIRO_CIRDIAG_CAPTURE_IDLE_MS);
	}
#else
	/* Summary only: arm once and leave it. The dump is never armed, so no
	 * accumulator is ever read and the ring stays empty and undrained. Diagnostic
	 * lines therefore keep flowing through the gap as well; the markers are
	 * interval boundaries, not a gate, and a parser keeps what falls between
	 * `capture` and its `end`. */
	LOG_WRN("CIR capture image: SUMMARY ONLY, %d ms per interval. NOT a shipping build.",
		CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOW_MS);
	uwb_cirdiag_set_enabled(true);

	for (uint32_t n = 1;; n++) {
		woz_printf("cir.cycle: n=%u capture\n", (unsigned)n);
		k_msleep(CONFIG_ALIRO_CIRDIAG_CAPTURE_WINDOW_MS);
		woz_printf("cir.cycle: n=%u end\n", (unsigned)n);
		k_msleep(CONFIG_ALIRO_CIRDIAG_CAPTURE_IDLE_MS);
	}
#endif
}

K_THREAD_DEFINE(cirdiag_capture_tid, CONFIG_ALIRO_CIRDIAG_CAPTURE_STACK, cirdiag_capture_thread,
		NULL, NULL, NULL, CONFIG_ALIRO_CIRDIAG_CAPTURE_PRIO, 0, 0);
