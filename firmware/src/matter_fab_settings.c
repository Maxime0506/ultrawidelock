/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * matter_fab_settings — see the header for why the port owns this.
 *
 * ONE KEY PER FIELD, not one blob for the lot. A single record would be about
 * 1.7 KB (two fabrics at ~500 B, a 254 B dataset, a 400 B ICAC slot) and would
 * need a buffer that size to build in. This node has ~8.7 KB of RAM left and
 * has already taken an MPU stack-guard fault on this board, so a 1.7 KB
 * temporary is not a neutral choice. Saving each field straight out of the
 * struct it already lives in costs no buffer at all.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "matter_fab_settings.h"

LOG_MODULE_REGISTER(matter_fab, CONFIG_LOG_DEFAULT_LEVEL);

#define FAB_TREE  "mfab"
#define KEY_VER   FAB_TREE "/ver"
#define KEY_FAB0  FAB_TREE "/f0"
#define KEY_FAB1  FAB_TREE "/f1"
#define KEY_TD    FAB_TREE "/td"
#define KEY_XP    FAB_TREE "/xp"
#define KEY_ICAC  FAB_TREE "/ic"
#define KEY_ICLEN FAB_TREE "/il"

/*
 * Bumped whenever any persisted struct changes shape.
 *
 * struct matter_fabric is stored raw, so its layout IS the format. The stored
 * size is checked against sizeof() on load and a mismatch discards the record:
 * a node that re-pairs is a nuisance, a node that reads a stale layout as a
 * NOC and an operational private key is a device that fails CASE with no
 * explanation at all.
 */
#define FAB_VERSION 1u

BUILD_ASSERT(MATTER_SUPPORTED_FABRICS == 2u,
	     "one settings key per fabric; add a key when the table grows");

/* Where a load puts what it finds. Set for the duration of matter_fab_load(). */
static struct matter_device_info *s_target;
static bool s_found_fabric;

static int fab_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next = NULL;
	struct matter_device_info *info = s_target;
	ssize_t got;

	if (info == NULL) {
		return 0;
	}

	if (settings_name_steq(name, "ver", &next)) {
		uint32_t ver = 0u;

		if (len != sizeof(ver) || read_cb(cb_arg, &ver, sizeof(ver)) < 0) {
			return -EINVAL;
		}
		if (ver != FAB_VERSION) {
			LOG_WRN("stored fabrics are version %u, this build wants %u -- discarding",
				(unsigned int)ver, FAB_VERSION);
			return -EINVAL;
		}
		return 0;
	}

	for (uint8_t i = 0u; i < MATTER_SUPPORTED_FABRICS; i++) {
		char key[3] = {'f', (char)('0' + i), '\0'};

		if (!settings_name_steq(name, key, &next)) {
			continue;
		}
		/*
		 * A short read here would leave HALF a fabric -- a valid index
		 * with a truncated NOC -- which answers Sigma1 and then fails
		 * every signature. Refuse the record instead.
		 */
		if (len != sizeof(info->fabrics[i])) {
			LOG_WRN("fabric %u is %u B, expected %u -- layout changed, discarding", i,
				(unsigned int)len, (unsigned int)sizeof(info->fabrics[i]));
			return -EINVAL;
		}
		got = read_cb(cb_arg, &info->fabrics[i], sizeof(info->fabrics[i]));
		if (got < 0) {
			return (int)got;
		}
		if (info->fabrics[i].index != 0u) {
			s_found_fabric = true;
		}
		return 0;
	}

	if (settings_name_steq(name, "td", &next)) {
		if (len > sizeof(info->thread_dataset)) {
			return -EINVAL;
		}
		got = read_cb(cb_arg, info->thread_dataset, len);
		if (got < 0) {
			return (int)got;
		}
		info->thread_dataset_len = (size_t)got;
		return 0;
	}

	if (settings_name_steq(name, "xp", &next)) {
		if (len != sizeof(info->thread_xpanid)) {
			return -EINVAL;
		}
		got = read_cb(cb_arg, info->thread_xpanid, sizeof(info->thread_xpanid));
		if (got < 0) {
			return (int)got;
		}
		info->have_thread_xpanid = true;
		return 0;
	}

	if (settings_name_steq(name, "il", &next)) {
		uint32_t both = 0u;

		if (len != sizeof(both) || read_cb(cb_arg, &both, sizeof(both)) < 0) {
			return -EINVAL;
		}
		info->icac.len = (size_t)(both & 0xFFFFu);
		info->icac.owner_index = (uint8_t)(both >> 16);
		return 0;
	}

	if (settings_name_steq(name, "ic", &next)) {
		if (len > sizeof(info->icac.buf)) {
			return -EINVAL;
		}
		got = read_cb(cb_arg, info->icac.buf, len);
		if (got < 0) {
			return (int)got;
		}
		return 0;
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(matter_fab, FAB_TREE, NULL, fab_set, NULL, NULL);

int matter_fab_store(const struct matter_device_info *info)
{
	uint32_t ver = FAB_VERSION;
	int rc;

	if (info == NULL) {
		return -EINVAL;
	}

	rc = settings_save_one(KEY_VER, &ver, sizeof(ver));
	if (rc != 0) {
		LOG_ERR("cannot store the fabric version (%d)", rc);
		return rc;
	}

	rc = settings_save_one(KEY_FAB0, &info->fabrics[0], sizeof(info->fabrics[0]));
	if (rc == 0) {
		rc = settings_save_one(KEY_FAB1, &info->fabrics[1], sizeof(info->fabrics[1]));
	}
	if (rc != 0) {
		LOG_ERR("cannot store a fabric (%d)", rc);
		return rc;
	}

	if (info->thread_dataset_len != 0u) {
		rc = settings_save_one(KEY_TD, info->thread_dataset, info->thread_dataset_len);
		if (rc != 0) {
			LOG_ERR("cannot store the Thread dataset (%d)", rc);
			return rc;
		}
	}
	if (info->have_thread_xpanid) {
		(void)settings_save_one(KEY_XP, info->thread_xpanid, sizeof(info->thread_xpanid));
	}

	/*
	 * The ICAC costs 400 B and Apple has never sent one (icac_len is 0 on
	 * every fabric this node has held), so it is written only when it
	 * exists rather than reserved against a case that has not occurred.
	 */
	if (info->icac.len != 0u) {
		uint32_t both = (uint32_t)info->icac.len | ((uint32_t)info->icac.owner_index << 16);

		rc = settings_save_one(KEY_ICLEN, &both, sizeof(both));
		if (rc == 0) {
			rc = settings_save_one(KEY_ICAC, info->icac.buf, info->icac.len);
		}
		if (rc != 0) {
			LOG_ERR("cannot store the intermediate certificate (%d)", rc);
			return rc;
		}
	}

	LOG_INF("operational identity stored (fabric %u/%u, dataset %u B)",
		(unsigned int)info->fabrics[0].index, (unsigned int)info->fabrics[1].index,
		(unsigned int)info->thread_dataset_len);
	return 0;
}

int matter_fab_load(struct matter_device_info *info)
{
	int rc;

	if (info == NULL) {
		return -EINVAL;
	}

	s_target = info;
	s_found_fabric = false;
	rc = settings_load_subtree(FAB_TREE);
	s_target = NULL;

	if (rc != 0) {
		/*
		 * A rejected record leaves the table PARTLY filled, and half an
		 * identity is worse than none: it advertises operational and
		 * cannot complete CASE. Clear what was read.
		 */
		LOG_WRN("stored fabrics unusable (%d); clearing and coming up commissionable", rc);
		memset(info->fabrics, 0, sizeof(info->fabrics));
		info->thread_dataset_len = 0u;
		info->have_thread_xpanid = false;
		info->icac.len = 0u;
		info->icac.owner_index = 0u;
		return rc;
	}

	if (!s_found_fabric) {
		return 1;
	}

	/*
	 * A stored record MEANS commissioning finished -- nothing writes one
	 * before CommissioningComplete. Restoring it as false leaves a fully
	 * commissioned node looking like it is mid-pairing, and the next
	 * commissioner to open a PASE session rolls back the very fabrics that
	 * were just restored. That silently destroys a working pairing on the
	 * first failed connection attempt after a reboot.
	 */
	info->commissioning_complete = true;

	LOG_INF("operational identity restored (fabric %u/%u, dataset %u B)",
		(unsigned int)info->fabrics[0].index, (unsigned int)info->fabrics[1].index,
		(unsigned int)info->thread_dataset_len);
	return 0;
}

int matter_fab_erase(void)
{
	static const char *const keys[] = { KEY_VER, KEY_FAB0,  KEY_FAB1, KEY_TD,
					    KEY_XP,  KEY_ICLEN, KEY_ICAC };
	int first_err = 0;

	/*
	 * Every return code checked. These were discarded behind a (void) and
	 * the function logged "erased" unconditionally, so a wipe that deleted
	 * NOTHING was indistinguishable from one that worked -- the board came
	 * back with the same fabrics, which reads as the erase never having been
	 * asked for rather than having failed.
	 */
	for (size_t i = 0u; i < ARRAY_SIZE(keys); i++) {
		int rc = settings_delete(keys[i]);

		if (rc != 0 && first_err == 0) {
			first_err = rc;
		}
		LOG_WRN("erase %s -> rc=%d", keys[i], rc);
	}
	if (first_err != 0) {
		LOG_ERR("operational identity NOT fully erased (first rc=%d)", first_err);
		return first_err;
	}
	LOG_WRN("operational identity erased");
	return 0;
}
