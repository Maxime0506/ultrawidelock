/**
 * @file test_uwb_seam.c — the decadriver seam's engine-less tier.
 *
 * File under test: modules/woz_uwb/src/driver/uwb_seam.h, compiled WITHOUT
 * CONFIG_WOZ_ALIRO -- the inline-to-decadriver half that no other build
 * compiles. Its own binary because one header compiled two ways in one binary
 * breaks coverage merging. It proves only that each helper forwards to the
 * decadriver entry point it claims, argument and return unchanged; the
 * shipping seam behaviour is test_ccc_shim.c's job.
 */
#include <stdio.h>
#include <string.h>

#include "test.h"

#include <deca_device_api.h>

#include "uwb_seam.h"

/* ---- decadriver doubles ---------------------------------------------------
 * Local rather than tests/host/shim/drvfake.c: that file also defines
 * woz_uwb_arm_rx and woz_uwb_set_sts_iv as real functions, which is precisely
 * the tier this binary exists to avoid linking. */

static struct {
	int32_t rxenable_mode;
	unsigned rxenable_calls;
	int32_t rxenable_ret;

	dwt_sts_cp_iv_t *last_iv;
	unsigned configurestsiv_calls;

	dwt_callbacks_s *last_callbacks;
	unsigned setcallbacks_calls;

	dwt_config_t *last_config;
	unsigned configure_calls;
	int32_t configure_ret;
} radio;

int32_t dwt_rxenable(int32_t mode)
{
	radio.rxenable_calls++;
	radio.rxenable_mode = mode;
	return radio.rxenable_ret;
}

void dwt_configurestsiv(dwt_sts_cp_iv_t *iv)
{
	radio.configurestsiv_calls++;
	radio.last_iv = iv;
}

void dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	radio.setcallbacks_calls++;
	radio.last_callbacks = callbacks;
}

int32_t dwt_configure(dwt_config_t *config)
{
	radio.configure_calls++;
	radio.last_config = config;
	return radio.configure_ret;
}

/* ---- the four helpers ------------------------------------------------------ */

static void test_uwb_seam(void)
{
	dwt_sts_cp_iv_t iv;
	dwt_callbacks_s callbacks;
	dwt_config_t config;

	t_group("uwb seam, no engine");

	memset(&radio, 0, sizeof(radio));
	memset(&iv, 0, sizeof(iv));
	memset(&callbacks, 0, sizeof(callbacks));
	memset(&config, 0, sizeof(config));

	/* Arming RX is the bare radio call at this tier: there is no CCC STS to
	 * program first, and the mode goes through untouched. */
	radio.rxenable_ret = 0;
	T_EQ("arm rx returns what the radio returned", woz_uwb_arm_rx(0), 0L);
	T_EQ("arm rx called the radio once", (long)radio.rxenable_calls, 1L);
	T_EQ("mode passed through", (long)radio.rxenable_mode, 0L);

	radio.rxenable_ret = -1;
	T_EQ("a radio failure is propagated", woz_uwb_arm_rx(2), -1L);
	T_EQ("mode passed through", (long)radio.rxenable_mode, 2L);
	T_EQ("arm rx called the radio twice", (long)radio.rxenable_calls, 2L);

	/* The STS-IV is loaded verbatim: with no engine bound there is no CCC
	 * STS-V to substitute for it. */
	woz_uwb_set_sts_iv(&iv);
	T_EQ("sts iv loaded once", (long)radio.configurestsiv_calls, 1L);
	T_OK("the caller's iv reached the radio", radio.last_iv == &iv);

	/* Callbacks are registered with no shim in front of them. */
	woz_uwb_set_callbacks(&callbacks);
	T_EQ("callbacks registered once", (long)radio.setcallbacks_calls, 1L);
	T_OK("the caller's table reached the radio", radio.last_callbacks == &callbacks);

	/* PHY configuration is untraced here, and its status is returned as-is. */
	radio.configure_ret = 0;
	T_EQ("configure returns what the radio returned", woz_uwb_configure_phy(&config), 0L);
	T_EQ("configure called once", (long)radio.configure_calls, 1L);
	T_OK("the caller's config reached the radio", radio.last_config == &config);

	radio.configure_ret = -1;
	T_EQ("a configure failure is propagated", woz_uwb_configure_phy(&config), -1L);
	T_EQ("configure called twice", (long)radio.configure_calls, 2L);
}

int main(void)
{
	test_uwb_seam();

	if (t_fail > 0) {
		printf("  uwb-seam: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	printf("  uwb-seam: PASS (%d checks — the engine-less inline tier, "
	       "forwarding only)\n",
	       t_pass);
	return 0;
}
