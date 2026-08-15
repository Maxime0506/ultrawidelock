/* SPDX-License-Identifier: ISC */

/*
 * This port's half of modules/ultrawidelock_uwb/include/uwb_seam.h.
 *
 * WHY THIS FILE EXISTS. The Zephyr build routes DW3000 RX events through
 * uwb_rxdiag.c, which owns ultrawidelock_uwb_set_callbacks and inserts a shim ahead of
 * the MAC's own RX-good callback. That shim is not a diagnostic despite living
 * in a diagnostics file: after the MAC arms the SP3 POLL window, it calls
 * ccc_shim_rx_try_prepoll to decrypt the Pre-POLL and warm the next block's
 * STS. That bootstrap warm is what makes g_warm_valid true, which is what makes
 * the POLL window ever get armed and Response_0 ever get sent.
 *
 * uwb_rxdiag.c is a Zephyr-module literal -- it is in no role manifest, because
 * its heartbeat is built on Zephyr work items -- so this port does not compile
 * it, exactly as ports/esp32 does not. Without this file the callbacks reach
 * the radio unmodified, ccc_shim_rx_try_prepoll is never called, and the
 * responder hears every Pre-POLL and answers none of them.
 *
 * So what is re-created here is the essential chain and nothing else: no work
 * items, no tallies, no cadence logging. It is the third copy of the same idea,
 * after the Zephyr original and the ESP-IDF one; a fourth consumer would be the
 * point at which it earns a role manifest of its own rather than a file per
 * port.
 *
 * The CIR diagnostics are absent rather than stubbed. uwb_cirdiag.h supplies
 * inline no-ops when CONFIG_ULTRAWIDELOCK_UWB_CIRDIAG is unset, and this port does not
 * set it, so calling them would compile to nothing; leaving them out says the
 * same thing without implying the feature is one define away from working here.
 */
#include <stdbool.h>
#include <stdint.h>

#include <deca_device_api.h>

#include "ccc_shim.h"
#include "uwb_seam.h"

/*
 * A decode-latency counter owned by the uwb_rxdiag.c this port omits.
 * ccc_shim_rx.c declares it extern and reads it in diagnostic log lines only,
 * so it has to exist for the image to link and stays zero because nothing here
 * stamps it.
 */
uint32_t g_ccc_dbg_decode;

/* Nothing to add to a PHY configuration without uwb_rxdiag.c's logging. */
int32_t ultrawidelock_uwb_configure_phy(dwt_config_t *config)
{
	return dwt_configure(config);
}

/* The MAC's own callbacks, kept so the shims below can chain to them. */
static dwt_cb_t g_chain_rxok;
static dwt_cb_t g_chain_rxto;
static dwt_cb_t g_chain_rxerr;
static dwt_cb_t g_chain_txdone;

/*
 * RX-good: run the MAC's arm first, then -- unless this reception is the POLL
 * the session is already waiting for -- decode the Pre-POLL to warm the next
 * block's STS.
 *
 * The order is the whole point. prepoll_rx_rearm has a deadline behind it and
 * the decode does not, so the arm goes first and the warm rides whatever time
 * is left in the block.
 */
static void shim_rxok(const dwt_cb_data_t *d)
{
	bool await = ccc_shim_rx_awaiting_poll();

	if (d != NULL) {
		ccc_shim_rx_notify_rx(d->status);
	}
	if (g_chain_rxok != NULL) {
		g_chain_rxok(d);
	}
	if (d != NULL && !await) {
		ccc_shim_rx_try_prepoll(d->datalength);
	}
}

static void shim_rxto(const dwt_cb_data_t *d)
{
	if (g_chain_rxto != NULL) {
		g_chain_rxto(d);
	}
}

static void shim_rxerr(const dwt_cb_data_t *d)
{
	if (g_chain_rxerr != NULL) {
		g_chain_rxerr(d);
	}
}

static void shim_txdone(const dwt_cb_data_t *d)
{
	if (g_chain_txdone != NULL) {
		g_chain_txdone(d);
	}
}

/*
 * Intercept the registration and insert the RX-good bootstrap shim.
 *
 * A null callback stays null rather than becoming a shim that forwards to
 * nothing: the decadriver reads the pointers to decide which interrupt sources
 * to enable, so substituting a live pointer for an absent one turns on an event
 * the MAC never asked for.
 */
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
