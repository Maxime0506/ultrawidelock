/**
 * @file test_uwb_selftest.c — the Kconfig-gated boot self-test (uwb_selftest.c)
 * against a fake facade. Pins the arm-at-init scheduling and the canned Aliro
 * ranging config the worker hands to woz_uwb_start_aliro — the facade itself
 * is a recording double, so no ranging path runs.
 */
#include <string.h>

#include <zephyr/kernel.h>

#include "drvfake.h"
#include "test.h"

extern int (*const logfake_sys_init_uwb_selftest_init)(void);

void test_uwb_selftest(void)
{
	t_group("boot arm");
	drvfake_reset();
	workfake.last = NULL;
	T_EQ("init rc", logfake_sys_init_uwb_selftest_init(), 0);
	T_OK("work scheduled", workfake.last != NULL);
	T_EQ("fires after the Kconfig delay", (long)workfake.last_delay,
	     (long)CONFIG_WOZ_UWB_SELFTEST_DELAY_MS);
	T_EQ("nothing started yet", (long)drvfake.start_aliro_calls, 0L);

	t_group("worker: canned config reaches the facade");
	workfake.last->work.handler(&workfake.last->work);
	T_EQ("wrap log budget reset first", (long)drvfake.wrap_log_reset_calls, 1L);
	T_EQ("start_aliro called", (long)drvfake.start_aliro_calls, 1L);
	T_EQ("session id", (long)drvfake.last_aliro_cfg.session_id, (long)0x02b02fd4u);
	T_EQ("channel 9", (long)drvfake.last_aliro_cfg.channel, 9L);
	T_EQ("sync code 9", (long)drvfake.last_aliro_cfg.sync_code_index, 9L);
	T_EQ("slot 2400 rstu", (long)drvfake.last_aliro_cfg.slot_duration_rstu, 2400L);
	T_EQ("block 192ms", (long)drvfake.last_aliro_cfg.block_duration_ms, 192L);
	T_EQ("12 slots per round", (long)drvfake.last_aliro_cfg.slot_per_round, 12L);
	T_EQ("sts_index0", (long)drvfake.last_aliro_cfg.sts_index0, (long)0x1196e79du);
	T_OK("uwb_time zero", drvfake.last_aliro_cfg.uwb_time_us == 0u);
	T_OK("dummy URSK forwarded",
	     drvfake.last_aliro_cfg.ursk != NULL && drvfake.last_aliro_ursk[0] == 0x11 &&
		     drvfake.last_aliro_ursk[15] == 0x00 && drvfake.last_aliro_ursk[31] == 0x00);
	T_OK("no ranging config (URSK fallback)",
	     drvfake.last_aliro_cfg.ranging_config == NULL && drvfake.last_aliro_cfg.rc_len == 0);

	t_group("worker: failure path only logs");
	drvfake.start_aliro_ret = -5;
	workfake.last->work.handler(&workfake.last->work);
	T_EQ("second run still calls through", (long)drvfake.start_aliro_calls, 2L);
}
