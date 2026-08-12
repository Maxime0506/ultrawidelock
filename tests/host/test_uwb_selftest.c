/**
 * @file test_uwb_selftest.c — the Kconfig-gated boot self-test (uwb_selftest.c)
 * against a fake facade and the host OSAL's virtual clock. Pins the
 * arm-at-init delay and the canned credential ranging config the worker hands to
 * ultrawidelock_uwb_start_ultrawidelock — the facade itself is a recording double, so no
 * ranging path runs.
 */
#include <string.h>

#include "drvfake.h"
#include "test.h"
#include "ultrawidelock_osal.h"

extern int (*const ultrawidelock_init_uwb_selftest_init)(void);

void test_uwb_selftest(void)
{
	t_group("boot arm");
	drvfake_reset();
	ultrawidelock_osal_host_reset();
	T_EQ("init rc", ultrawidelock_init_uwb_selftest_init(), 0);
	T_EQ("nothing started yet", (long)drvfake.start_ultrawidelock_calls, 0L);
	T_EQ("quiet until the Kconfig delay",
	     (long)ultrawidelock_osal_host_advance_ms(CONFIG_ULTRAWIDELOCK_UWB_SELFTEST_DELAY_MS - 1), 0L);
	T_EQ("fires after the Kconfig delay", (long)ultrawidelock_osal_host_advance_ms(1), 1L);

	t_group("worker: canned config reaches the facade");
	T_EQ("wrap log budget reset first", (long)drvfake.wrap_log_reset_calls, 1L);
	T_EQ("start_ultrawidelock called", (long)drvfake.start_ultrawidelock_calls, 1L);
	T_EQ("session id", (long)drvfake.last_ultrawidelock_cfg.session_id, (long)0x02b02fd4u);
	T_EQ("channel 9", (long)drvfake.last_ultrawidelock_cfg.channel, 9L);
	T_EQ("sync code 9", (long)drvfake.last_ultrawidelock_cfg.sync_code_index, 9L);
	T_EQ("slot 2400 rstu", (long)drvfake.last_ultrawidelock_cfg.slot_duration_rstu, 2400L);
	T_EQ("block 192ms", (long)drvfake.last_ultrawidelock_cfg.block_duration_ms, 192L);
	T_EQ("12 slots per round", (long)drvfake.last_ultrawidelock_cfg.slot_per_round, 12L);
	T_EQ("sts_index0", (long)drvfake.last_ultrawidelock_cfg.sts_index0, (long)0x1196e79du);
	T_OK("uwb_time zero", drvfake.last_ultrawidelock_cfg.uwb_time_us == 0u);
	T_OK("dummy URSK forwarded",
	     drvfake.last_ultrawidelock_cfg.ursk != NULL && drvfake.last_ultrawidelock_ursk[0] == 0x11 &&
		     drvfake.last_ultrawidelock_ursk[15] == 0x00 && drvfake.last_ultrawidelock_ursk[31] == 0x00);
	T_OK("no ranging config (URSK fallback)",
	     drvfake.last_ultrawidelock_cfg.ranging_config == NULL && drvfake.last_ultrawidelock_cfg.rc_len == 0);

	t_group("worker: failure path only logs");
	drvfake.start_ultrawidelock_ret = -5;
	T_EQ("re-arm rc", ultrawidelock_init_uwb_selftest_init(), 0);
	T_EQ("worker fires again",
	     (long)ultrawidelock_osal_host_advance_ms(CONFIG_ULTRAWIDELOCK_UWB_SELFTEST_DELAY_MS), 1L);
	T_EQ("second run still calls through", (long)drvfake.start_ultrawidelock_calls, 2L);
}
