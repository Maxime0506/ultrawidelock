/** @file test_facade.c — woz_uwb_facade: bind/start/stop + range readback. */
#include <errno.h>
#include <string.h>

#include "aliro_kdf.h" /* ALIRO_URSK_LEN */
#include "ccc_shim.h"
#include "fira_session.h"
#include "woz_uwb_facade.h"
#include "test.h"

static int s_facade_listener_hits;

static void facade_range_listener(void)
{
	s_facade_listener_hits++;
}

void test_facade(void)
{
	uint8_t ursk[ALIRO_URSK_LEN];
	uint8_t rc[17];
	struct woz_uwb_aliro_cfg c;
	int32_t cm = -1;

	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}
	for (size_t i = 0; i < sizeof(rc); i++) {
		rc[i] = (uint8_t)i;
	}

	t_group("prewarm applies the session PHY with the radio left off");
	T_EQ("prewarm.ok", woz_uwb_prewarm(9u, 9u), 0);

	t_group("range-listener seam registers and clears");
	s_facade_listener_hits = 0;
	woz_uwb_set_range_listener(facade_range_listener);
	fira_session_set_ccc_range_cm(140, 1u); /* accepted latch -> callback */
	T_EQ("listener.fired", s_facade_listener_hits, 1);
	woz_uwb_set_range_listener(NULL);
	fira_session_set_ccc_range_cm(141, 2u);
	T_EQ("listener.cleared", s_facade_listener_hits, 1);

	t_group("bind_ursk binds the CCC shim");
	T_OK("shim.unbound.before", !ccc_shim_active());
	T_EQ("bind.ok", woz_uwb_bind_ursk(ursk, sizeof(ursk)), 0);
	T_OK("shim.bound.after", ccc_shim_active());

	t_group("start_aliro rejects null cfg / null ursk");
	T_EQ("start.null", woz_uwb_start_aliro(NULL), -EINVAL);
	memset(&c, 0, sizeof(c));
	c.ursk = NULL;
	T_EQ("start.null.ursk", woz_uwb_start_aliro(&c), -EINVAL);

	t_group("start_aliro with a serialized RangingConfiguration");
	memset(&c, 0, sizeof(c));
	c.session_id = 0x11223344u;
	c.channel = 5u;
	c.sync_code_index = 9u;
	c.slot_duration_rstu = 1200u;
	c.block_duration_ms = 768u;
	c.slot_per_round = 6u;
	c.sts_index0 = 0x1000u;
	c.uwb_time_us = 0u;
	c.ursk = ursk;
	c.ranging_config = rc;
	c.rc_len = sizeof(rc);
	T_EQ("start.rc", woz_uwb_start_aliro(&c), 0);
	T_OK("shim.active.rc", ccc_shim_active());

	t_group("start_aliro URSK fallback (no ranging_config, slot_per_round 0)");
	c.ranging_config = NULL;
	c.rc_len = 0u;
	c.slot_per_round = 0u;
	T_EQ("start.fallback", woz_uwb_start_aliro(&c), 0);

	t_group("stop unbinds the shim");
	woz_uwb_stop();
	T_OK("shim.unbound", !ccc_shim_active());

	t_group("last_range_cm reflects the fira range store");
	fira_session_set_ccc_range_cm(150, 3u);
	T_OK("range.present", woz_uwb_last_range_cm(&cm));
	T_EQ("range.cm", cm, 150);

	t_group("trusted_range_cm gates the unlock seam on layer-4 consensus");
	/* Four agreeing blocks saturate trust at K regardless of prior state. */
	fira_session_set_ccc_range_cm(150, 4u);
	fira_session_set_ccc_range_cm(150, 5u);
	fira_session_set_ccc_range_cm(150, 6u);
	fira_session_set_ccc_range_cm(150, 7u);
	cm = -1;
	T_OK("trusted.present", woz_uwb_trusted_range_cm(&cm));
	T_EQ("trusted.cm", cm, 150);
	/* A spoofed (implausible) block clears trust and does not latch: the raw
	 * accessor still returns the last good range, but the trusted accessor
	 * (what the unlock seam uses) refuses it. */
	fira_session_set_ccc_range_cm(-400, 8u);
	cm = -1;
	T_OK("spoof.raw.kept", woz_uwb_last_range_cm(&cm));
	T_EQ("spoof.raw.cm", cm, 150);
	T_OK("spoof.trusted.refused", !woz_uwb_trusted_range_cm(&cm));

	/* Leave the fira store cleared for any later reader. */
	fira_session_set_provisioned_ursk(NULL);
}
