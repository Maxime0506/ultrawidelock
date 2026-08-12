/** @file test_ultrawidelock_uwb_adapter.c — reader-context lifecycle + error mapping. */
#include <stdint.h>
#include <string.h>

#include "ultrawidelock_uwb_internal.h" /* struct ultrawidelock_uwb_adapter + cherry_err_to_ultrawidelock */
#include "test.h"

static struct cherry_ccc_capabilities mk_caps(uint16_t *pvs, uint16_t *cfgs,
					      uint8_t *combos)
{
	struct cherry_ccc_capabilities c;

	memset(&c, 0, sizeof(c));
	c.protocol_versions.len = 1u;
	c.protocol_versions.items = pvs;
	c.uwb_configs.len = 1u;
	c.uwb_configs.items = cfgs;
	c.pulse_shape_combos.len = 1u;
	c.pulse_shape_combos.items = combos;
	c.minimum_ran_multiplier = 4u;
	return c;
}

static struct ultrawidelock_uwb_adapter_reader_config mk_cfg(void)
{
	struct ultrawidelock_uwb_adapter_reader_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.min_ran_multiplier = 8u;
	cfg.preferred_hopping_configs.count = 1u;
	cfg.preferred_hopping_configs.configs[0] =
		ULTRAWIDELOCK_HOPPING_CONFIG_CONTINUOUS_DEFAULT;
	return cfg;
}

void test_ultrawidelock_uwb_adapter(void)
{
	int dummy = 0;
	struct cherry *ctx = (struct cherry *)&dummy;
	uint16_t pvs[1] = { 0x0100u };
	uint16_t cfgs[1] = { 0x0001u };
	uint8_t combos[1] = { 0x00u };
	struct cherry_ccc_capabilities ccc = mk_caps(pvs, cfgs, combos);
	struct cherry_core_event_device_capabilities caps;
	struct ultrawidelock_uwb_adapter_reader_config cfg = mk_cfg();

	memset(&caps, 0, sizeof(caps));
	caps.ccc_capabilities = &ccc;

	t_group("create_reader happy path + diagnostics + destroy");
	struct ultrawidelock_uwb_adapter *a = ultrawidelock_uwb_adapter_create_reader(ctx, &caps, &cfg);
	T_OK("create", a != NULL);
	/* min_ran_multiplier resolves to max(config, caps). */
	T_EQ("min_ran", a->min_ran_multiplier, 8u);
	struct cherry_common_diag_cfg diag;
	memset(&diag, 0, sizeof(diag));
	diag.aoa = true;
	ultrawidelock_uwb_adapter_set_diagnostics(a, diag); /* first: allocates */
	ultrawidelock_uwb_adapter_set_diagnostics(a, diag); /* second: copies into existing */
	ultrawidelock_uwb_adapter_set_diagnostics(NULL, diag); /* no-op */
	ultrawidelock_uwb_adapter_destroy(a);
	ultrawidelock_uwb_adapter_destroy(NULL); /* no-op */

	t_group("create_reader rejects bad input");
	T_OK("null.cherry", ultrawidelock_uwb_adapter_create_reader(NULL, &caps, &cfg) == NULL);
	T_OK("null.caps", ultrawidelock_uwb_adapter_create_reader(ctx, NULL, &cfg) == NULL);
	T_OK("null.config", ultrawidelock_uwb_adapter_create_reader(ctx, &caps, NULL) == NULL);

	struct ultrawidelock_uwb_adapter_reader_config bad = mk_cfg();
	bad.preferred_hopping_configs.count = 0u;
	T_OK("hop.count0", ultrawidelock_uwb_adapter_create_reader(ctx, &caps, &bad) == NULL);
	bad.preferred_hopping_configs.count =
		ULTRAWIDELOCK_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX + 1u;
	T_OK("hop.toobig", ultrawidelock_uwb_adapter_create_reader(ctx, &caps, &bad) == NULL);

	struct ultrawidelock_uwb_adapter_reader_config nodef = mk_cfg();
	nodef.preferred_hopping_configs.configs[0] = ULTRAWIDELOCK_HOPPING_CONFIG_DISABLED;
	T_OK("hop.nodefault", ultrawidelock_uwb_adapter_create_reader(ctx, &caps, &nodef) == NULL);

	struct cherry_core_event_device_capabilities nocaps;
	memset(&nocaps, 0, sizeof(nocaps));
	nocaps.ccc_capabilities = NULL;
	T_OK("caps.no_ccc", ultrawidelock_uwb_adapter_create_reader(ctx, &nocaps, &cfg) == NULL);

	t_group("cherry_err -> ultrawidelock_err mapping");
	T_EQ("e.none", cherry_err_to_ultrawidelock(CHERRY_ERR_NONE), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("e.param", cherry_err_to_ultrawidelock(CHERRY_ERR_INVALID_PARAMETER),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("e.timeout", cherry_err_to_ultrawidelock(CHERRY_ERR_UWBS_TIMEOUT),
	     ULTRAWIDELOCK_UWB_ERR_UWBS_TIMEOUT);
	T_EQ("e.internal", cherry_err_to_ultrawidelock(CHERRY_ERR_INTERNAL),
	     ULTRAWIDELOCK_UWB_ERR_INTERNAL);
	T_EQ("e.sinit", cherry_err_to_ultrawidelock(CHERRY_ERR_SESSION_INIT),
	     ULTRAWIDELOCK_UWB_ERR_SESSION_INIT);
	T_EQ("e.sactive", cherry_err_to_ultrawidelock(CHERRY_ERR_SESSION_ACTIVE),
	     ULTRAWIDELOCK_UWB_ERR_SESSION_ACTIVE);
	T_EQ("e.sconfig", cherry_err_to_ultrawidelock(CHERRY_ERR_SESSION_CONFIG),
	     ULTRAWIDELOCK_UWB_ERR_SESSION_CONFIG);
	T_EQ("e.notsup", cherry_err_to_ultrawidelock(CHERRY_ERR_SESSION_TYPE_NOT_SUPPORTED),
	     ULTRAWIDELOCK_UWB_ERR_INTERNAL);
	T_EQ("e.default", cherry_err_to_ultrawidelock((enum cherry_err)999), ULTRAWIDELOCK_UWB_ERR_INTERNAL);
}
