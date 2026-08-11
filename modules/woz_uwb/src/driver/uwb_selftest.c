/** @file uwb_selftest.c — Kconfig-gated one-shot UWB init self-test (no iPhone). */

#include <ultrawidelock/uwb.h>
#include "ccc_shim.h"
#include "woz_log.h"
#include "woz_osal.h"

LOG_MODULE_REGISTER(woz_uwb_selftest, LOG_LEVEL_INF);

// Delayable work item for the UWB self-test boot diagnostic; scheduled once at startup if
// UWB_SELFTEST=1.
static struct woz_dwork uwb_selftest_dwork;

/** Canned Aliro ranging config for the peerless self-test (dummy URSK). */
static const uint8_t uwb_selftest_ursk[32] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
	0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
};

/**
 * @brief One-shot worker: run the Aliro UWB start path and log the outcome.
 * @param dwork The delayable work item (unused).
 */
static void uwb_selftest_work(struct woz_dwork *dwork)
{
	// Configuration struct for the Aliro DS-TWR responder, containing ranging parameters
	// (channel, preamble code, session ID, and STS index).
	const struct woz_uwb_aliro_cfg cfg = {
		.session_id = 0x02b02fd4u,
		.channel = 9u,
		.sync_code_index = 9u,
		.slot_duration_rstu = 2400u,
		.block_duration_ms = 192u,
		.slot_per_round = 12u,
		.sts_index0 = 0x1196e79du,
		.uwb_time_us = 0u,
		.ursk = uwb_selftest_ursk,
	};
	int rc;

	(void)dwork;

	/* Reset the per-boot wrap-log budget so the first CCC STS IVs print. */
	ccc_shim_wrap_log_reset();

	LOG_INF("SELFTEST: woz_uwb_start_aliro() [responder-RX wrap probe] ...");
	rc = woz_uwb_start_aliro(&cfg);
	LOG_INF("SELFTEST: woz_uwb_start_aliro() = %d %s", rc,
		rc == 0 ? "(reached ACTIVE)" : "(FAILED -- see DIAG above)");
}

/**
 * @brief Arm the one-shot self-test at application init.
 * @return 0 on success.
 */
static int uwb_selftest_init(void)
{
	woz_dwork_init(&uwb_selftest_dwork, uwb_selftest_work);
	LOG_INF("SELFTEST: UWB init self-test armed, firing in %d ms",
		CONFIG_WOZ_UWB_SELFTEST_DELAY_MS);
	woz_dwork_schedule(&uwb_selftest_dwork, CONFIG_WOZ_UWB_SELFTEST_DELAY_MS);
	return 0;
}

WOZ_INIT_APPLICATION(uwb_selftest_init);
