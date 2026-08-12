/*
 * Anchor-to-anchor UWB ranging: stage A of the two-anchor plan
 *.
 *
 * One application, two roles, two boards. The role is CONFIG_ANCHOR_ROLE_*, the
 * board is the overlay in boards/. Nothing here is credential-aware: this builds at
 * the CONFIG_ULTRAWIDELOCK_UWB tier, where uwb_seam.h inlines straight to the decadriver
 * and there is no credential, no STS engine and no phone in the loop. That is
 * the whole reason stage A is cheap, and it is why moving the satellite to a
 * second DWM3001CDK later is a board string rather than a port.
 *
 * Everything runs in this thread. The DS-TWR exchange polls SYS_STATUS rather
 * than taking DW3000 callbacks, so the standing rule for this hardware --
 * nothing blocking on a ranging callback, because the delayed-TX reply window
 * is measured in microseconds -- holds by construction.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "anchor_twr.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * There is deliberately no `ultrawidelock_uwb_diag_on = 0` here, unlike
 * apps/dwm3001cdk-lock/src/main.c.
 *
 * That flag and every DIAGK call site live in ccc_shim_rx.c and uwb_rxdiag.c,
 * which ultrawidelock_uwb compiles only at the CONFIG_ULTRAWIDELOCK_CRED tier. This app is below
 * it, so the per-frame trace is not merely switched off, it does not exist --
 * the symbol is undefined and referencing it fails the link. The rule the lock
 * has to enforce at runtime is enforced here by the build.
 */

/** How often the counters are summarised, in completed rounds. */
#define REPORT_EVERY 100u

/**
 * One line per report interval, in a form a host script can parse without
 * knowing anything about this firmware: `ANCHOR` then key=value pairs.
 *
 * Printed from thread context between rounds, never from the exchange, and
 * deliberately not once per round -- at the initiator's cadence a per-round
 * line is a print every few tens of milliseconds, which is exactly the load
 * this board's ranging path cannot absorb.
 */
static void report(void)
{
	struct anchor_twr_stats s;

	anchor_twr_stats(&s);
	/* raw beside median on purpose: their difference is the half-chip slip, and
	 * a run where they stop diverging is a run where the filter is unnecessary. */
	/* t5err as a RANGE, not a maximum: the spread is what says whether the
	 * delayed-TX prediction is the constant ANCHOR_ANT_DLY_DTU absorbs. A
	 * min above the max means no delayed TX has completed yet. */
	LOG_INF("ANCHOR rounds=%u ranges=%u to=%u late=%u rej=%u t5err=%u..%u "
		"last_mm=%d raw_mm=%d fp=%u xtal=%d",
		(unsigned int)s.rounds, (unsigned int)s.ranges, (unsigned int)s.timeouts,
		(unsigned int)s.late, (unsigned int)s.rejected, (unsigned int)s.t5_err_min,
		(unsigned int)s.t5_err_max, (int)s.last_mm, (int)s.last_raw_mm,
		(unsigned int)s.last_fp_index, (int)s.last_xtal_offset);
}

/**
 * Entry point. Brings the DW3000 up, applies the anchor PHY, then loops on the
 * configured role for ever. Returns non-zero only if the radio never came up,
 * which on this board is a wiring or power fault rather than a software one.
 */
int main(void)
{
	uint32_t seq = 0;
	int rc;

	/* ASCII only: RTT is a byte stream and a UTF-8 dash renders as mojibake. */
	LOG_INF("ultrawidelock anchor: %s role",
		IS_ENABLED(CONFIG_ANCHOR_ROLE_INITIATOR) ? "INITIATOR" : "RESPONDER");

	rc = anchor_twr_init();
	if (rc != 0) {
		LOG_ERR("anchor_twr_init rc=%d", rc);
		return rc;
	}

	while (1) {
		if (IS_ENABLED(CONFIG_ANCHOR_ROLE_INITIATOR)) {
			(void)anchor_twr_initiator_round(seq);
			seq++;
			/* The initiator sets the cadence for both boards; the
			 * responder simply waits. Sleeping only here is what keeps
			 * the responder's receiver armed across the whole gap. */
			k_msleep(CONFIG_ANCHOR_ROUND_PERIOD_MS);
		} else {
			int32_t mm = 0;
			uint32_t rx_seq = 0;

			(void)anchor_twr_responder_round(&mm, &rx_seq);
			/* Counted locally rather than from rx_seq: the initiator's
			 * sequence restarts whenever that board reboots, and a
			 * report cadence driven off it would then stall. */
			seq++;
		}

		if ((seq % REPORT_EVERY) == 0u) {
			report();
		}
	}
	return 0;
}
