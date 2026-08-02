/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prov (Zephyr settings backend) — the DWM3001CDK twin of the ESP32
 * port's aliro_prov_nvs.c. The portable serialisation, dev fallback and trust
 * logic all live in aliro_prov.c (host-KAT'd); this file only moves that one
 * blob in and out of the settings store on `storage_partition`.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "aliro_prov.h"

LOG_MODULE_REGISTER(aliro_prov, CONFIG_LOG_DEFAULT_LEVEL);

#define ALIRO_PROV_KEY "aliro/prov"

static uint8_t s_blob[ALIRO_PROV_BLOB_MAX];
static size_t s_blob_len;

static int prov_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(name);

	if (len > sizeof(s_blob)) {
		return -EINVAL;
	}

	ssize_t got = read_cb(cb_arg, s_blob, len);

	if (got < 0) {
		return (int)got;
	}
	s_blob_len = (size_t)got;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(aliro_prov, "aliro", NULL, prov_set, NULL, NULL);

int aliro_prov_load(struct aliro_reader_identity *id, struct aliro_trust_store *ts)
{
	int rc = settings_subsys_init();

	if (rc != 0) {
		LOG_WRN("settings init rc=%d; using DEV identity", rc);
		aliro_prov_dev_default(id, ts);
		return -1;
	}

#if IS_ENABLED(CONFIG_ALIRO_PROV_CLEAR_ON_BOOT)
	/* Before the load, not after: the point is that nothing ever sees the old
	 * blob, so the identity this boot reports is the DEV one. */
	rc = settings_delete(ALIRO_PROV_KEY);
	LOG_WRN("clear-on-boot: erased " ALIRO_PROV_KEY " (rc=%d)", rc);
#endif

	s_blob_len = 0;
	rc = settings_load_subtree("aliro");
	if (rc != 0) {
		LOG_WRN("settings load rc=%d; using DEV identity", rc);
		aliro_prov_dev_default(id, ts);
		return -1;
	}

	if (s_blob_len == 0) {
		/* Never provisioned. */
		aliro_prov_dev_default(id, ts);
		return 1;
	}

	if (aliro_prov_deserialize(s_blob, s_blob_len, id, ts) != 0) {
		LOG_WRN("stored blob malformed; using DEV identity");
		aliro_prov_dev_default(id, ts);
		return -1;
	}
	return 0;
}

int aliro_prov_erase(void)
{
	int rc = settings_subsys_init();

	if (rc != 0) {
		LOG_ERR("settings init rc=%d; nothing erased", rc);
		return rc;
	}
	rc = settings_delete(ALIRO_PROV_KEY);
	/* The rc is reported, not swallowed. A factory reset that quietly did
	 * nothing is worse than one that fails loudly: the board comes back
	 * looking reset, pairs, and then rejects the phone with the old
	 * anchors still in the store. */
	LOG_WRN("factory reset: erased " ALIRO_PROV_KEY " (rc=%d)", rc);
	return rc;
}

int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)
{
	/*
	 * STATIC, not on the stack. ALIRO_PROV_BLOB_MAX scales with
	 * ALIRO_TRUST_MAX, and raising that 4 -> 8 put this at 864 B on a frame
	 * that also holds a 778 B struct aliro_trust_store. The result was an
	 * MPU fault through the bottom of the 4 KB main stack about four
	 * seconds into boot -- the board advertised, froze, and the phone
	 * reported "transaction timed out" with nothing in the log after the
	 * advert line.
	 *
	 * Safe as static because every caller reaches here from the reader's
	 * provisioning paths, which are serialised on s_prov_lock or run on the
	 * Matter work queue, and provisioning writes are rare besides.
	 */
	static uint8_t blob[ALIRO_PROV_BLOB_MAX];
	size_t len = 0;

	if (aliro_prov_serialize(id, ts, blob, sizeof(blob), &len) != 0) {
		return -EINVAL;
	}
	return settings_save_one(ALIRO_PROV_KEY, blob, len);
}
