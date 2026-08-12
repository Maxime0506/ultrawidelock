/* This port's half of modules/ultrawidelock_uwb/include/uwb_seam.h.
 *
 * The Nordic build routes DW3000 RX events through uwb_rxdiag.c's
 * ultrawidelock_uwb_set_callbacks -> shim_rxok, which (after the MAC's own
 * prepoll_rx_rearm arms the SP3 POLL window) calls ccc_shim_rx_try_prepoll to
 * decrypt+warm the NEXT block's STS.  That bootstrap warm is what flips
 * g_warm_valid true so the POLL window ever gets armed and Response_0 sent.
 *
 * This port omits uwb_rxdiag.c wholesale (its heartbeat needs Zephyr k_work,
 * which the compat layer does not provide), so without this shim the callbacks
 * reach the radio unmodified, ccc_shim_rx_try_prepoll is never called,
 * g_warm_valid stays false, and the responder receives Pre-POLLs but never
 * replies.  Re-create only the essential chain here (no k_work, no diagnostics),
 * plus the PHY-config seam, which this port has nothing to add to. */
#include <stdint.h>

#include <deca_device_api.h>

#include "ccc_shim.h"    /* ccc_shim_rx_{awaiting_poll,notify_rx,try_prepoll} */
#include "uwb_cirdiag.h" /* per-reception CIA diag latch (rides the `lab on` gate) */
#include "uwb_seam.h"    /* the two seams uwb_rxdiag.c would have supplied */

/* Diagnostic decode-latency counter owned by the omitted uwb_rxdiag.c; ccc_shim_rx.c
 * reads it (extern) only in DIAG log lines. Define it here so those references
 * resolve (stays 0 without the rxdiag stamper — diagnostics only). */
uint32_t g_ccc_dbg_decode;

/* PHY configuration: nothing to log without uwb_rxdiag.c, so pass it straight on. */
int32_t ultrawidelock_uwb_configure_phy(dwt_config_t *config)
{
	return dwt_configure(config);
}

/* The MAC's own callbacks, saved so our shims chain to them. */
static dwt_cb_t g_chain_rxok, g_chain_rxto, g_chain_rxerr, g_chain_txdone;

/* RX-good shim: feed the empirical STS-index tracker, run the MAC's arm
 * (prepoll_rx_rearm), then — unless this RX is the awaited POLL — decode the
 * Pre-POLL to warm the next block's STS.  Mirrors uwb_rxdiag.c:shim_rxok minus
 * the tallies/cadence/event logging. */
static void shim_rxok(const dwt_cb_data_t *d)
{
	bool await = ccc_shim_rx_awaiting_poll();

	if (d != NULL) {
		ccc_shim_rx_notify_rx(d->status);
	}
	/* Channel-impulse: sampled BEFORE the MAC re-arms, this says the reception being
	 * serviced is the Final — and the radio is still idle, which is the only state in which
	 * the accumulator can be read (see ccc_shim_rx_awaiting_final). Take the whole snapshot,
	 * window included, here: the Final owes the block nothing, so the ~192 ms inter-block gap
	 * absorbs the read before the SP0 listen goes back up. */
	bool is_final = ccc_shim_rx_awaiting_final();
	bool win = is_final && uwb_cirdiag_window_due();

	if (win) {
		(void)uwb_cirdiag_capture(d != NULL ? d->status : 0u,
					  d != NULL ? d->datalength : 0u, false, is_final);
	}
	if (g_chain_rxok != NULL) {
		g_chain_rxok(d);
	}
	if (d != NULL && !await) {
		ccc_shim_rx_try_prepoll(d->datalength);
	}
	/* Every other reception: summary only (cheap, bench-proven safe), taken after the arm so
	 * the POLL/Final deadlines are met first. is_final carries which reception this was for
	 * the FINAL_ONLY latch, mirroring uwb_rxdiag.c. dw3000_isr_task emits the [ALAB] line
	 * after its IRQ drain loop. */
	if (!win) {
		(void)uwb_cirdiag_capture(d != NULL ? d->status : 0u,
					  d != NULL ? d->datalength : 0u, true, is_final);
	}
}

// RX-timeout callback shim; forwards the event to g_chain_rxto if a handler is registered, otherwise no-op.
static void shim_rxto(const dwt_cb_data_t *d)
{
	if (g_chain_rxto != NULL) {
		g_chain_rxto(d);
	}
}

// RX-error callback shim; forwards the event to g_chain_rxerr if a handler is registered, otherwise no-op.
static void shim_rxerr(const dwt_cb_data_t *d)
{
	if (g_chain_rxerr != NULL) {
		g_chain_rxerr(d);
	}
}

// TX-done callback shim; forwards the event to g_chain_txdone if a handler is registered, otherwise no-op.
static void shim_txdone(const dwt_cb_data_t *d)
{
	if (g_chain_txdone != NULL) {
		g_chain_txdone(d);
	}
}

/* Intercept the callback registration and insert the RX-good bootstrap shim. */
void ultrawidelock_uwb_set_callbacks(dwt_callbacks_s *callbacks)
{
	if (callbacks != NULL) {
		g_chain_rxok = callbacks->cbRxOk;
		g_chain_rxto = callbacks->cbRxTo;
		g_chain_rxerr = callbacks->cbRxErr;
		g_chain_txdone = callbacks->cbTxDone;
		callbacks->cbRxOk = (g_chain_rxok != NULL) ? shim_rxok : NULL;
		callbacks->cbRxTo = (g_chain_rxto != NULL) ? shim_rxto : NULL;
		callbacks->cbRxErr = (g_chain_rxerr != NULL) ? shim_rxerr : NULL;
		callbacks->cbTxDone = (g_chain_txdone != NULL) ? shim_txdone : NULL;
	}
	dwt_setcallbacks(callbacks);
}
