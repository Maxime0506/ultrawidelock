/**
 * @file matter_settings_freertos.c — Zephyr's settings API over the port's store.
 *
 * The shared Matter Thread transport keeps exactly one thing here: the SRP
 * host-name suffix, a 32-bit value minted once per lifetime. Everything about
 * this file is sized by that.
 *
 * WHY THE PATH TABLE IS FIXED, and why an unknown path is an error rather than
 * a miss: Zephyr's settings are a string-keyed tree, and this port's store is a
 * flat uint16_t keyspace. A general translation would have to invent a hash,
 * and a hash collision here would silently alias two records. A table cannot
 * collide, and it makes the set of things Matter persists something you can
 * read in one screen.
 *
 * The failure that matters is a path this table does not know. A tree-shaped
 * backend would answer "nothing stored", which is exactly what a first boot
 * looks like -- so the caller would mint a fresh value, fail to save it, and do
 * the same again next boot, with the only symptom a name that changes every
 * time. This backend refuses instead, and says which path.
 */

#include <zephyr/settings/settings.h>

#include <string.h>

#include <ultrawidelock_freertos_kv.h>
#include <ultrawidelock_freertos_platform.h>

#define SETTINGS_TAG "matter_settings"

/** One entry per record Matter persists. */
struct ultrawidelock_matter_setting {
	const char *path;
	uint16_t key;
	size_t len;
};

/*
 * SRP_HOST_ID_KEY in the shared transport. Kept in step by
 * scripts/freertos-matter-source-check.sh, which fails the build if the string
 * on the other side ever stops matching this one -- a rename there would
 * otherwise land here as "unknown path" at run time, on a board.
 */
static const struct ultrawidelock_matter_setting s_settings[] = {
	{ "srp/hid", ULTRAWIDELOCK_KV_KEY_MATTER_SRP_HOST_ID, sizeof(uint32_t) },
};

static const struct ultrawidelock_matter_setting *lookup(const char *path)
{
	size_t i;

	for (i = 0; i < sizeof(s_settings) / sizeof(s_settings[0]); i++) {
		if (strcmp(s_settings[i].path, path) == 0) {
			return &s_settings[i];
		}
	}
	return NULL;
}

/*
 * The store is brought up by the board before any of this runs, and doing it
 * twice is not free -- it walks the flash. Zephyr's call is idempotent and the
 * transport relies on that, so this is the honest translation of "already
 * done".
 */
int settings_subsys_init(void)
{
	return 0;
}

/** Copies out of the record this backend already holds; no second read. */
static ssize_t direct_read(void *cb_arg, void *data, size_t len)
{
	const uint8_t *src = cb_arg;

	memcpy(data, src, len);
	return (ssize_t)len;
}

int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb, void *param)
{
	const struct ultrawidelock_matter_setting *s = lookup(subtree);
	uint8_t value[sizeof(uint32_t)];
	size_t length = sizeof(value);

	if (s == NULL) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, SETTINGS_TAG,
				 "no key for settings path '%s'", subtree);
		return -22; /* -EINVAL */
	}
	if (s->len > sizeof(value)) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, SETTINGS_TAG,
				 "'%s' is larger than this backend carries", subtree);
		return -22;
	}
	if (ultrawidelock_freertos_kv_get(s->key, value, &length) != 0) {
		/* Nothing stored yet. Zephyr reports this by not calling the
		 * callback, and the caller reads that as "mint one". */
		return 0;
	}
	/*
	 * A record of the wrong size is not a record. Handing it to the
	 * callback would have it read sizeof(uint32_t) bytes out of a shorter
	 * buffer; dropping it makes the next boot mint a fresh value, which is
	 * the recoverable outcome.
	 */
	if (length != s->len) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_WARNING, SETTINGS_TAG,
				 "'%s' stored as %u bytes, expected %u; ignoring",
				 subtree, (unsigned)length, (unsigned)s->len);
		return 0;
	}
	return cb("", length, direct_read, value, param);
}

int settings_save_one(const char *name, const void *value, size_t val_len)
{
	const struct ultrawidelock_matter_setting *s = lookup(name);

	if (s == NULL) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, SETTINGS_TAG,
				 "no key for settings path '%s'", name);
		return -22;
	}
	if (val_len != s->len) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, SETTINGS_TAG,
				 "'%s' saved as %u bytes, table says %u", name,
				 (unsigned)val_len, (unsigned)s->len);
		return -22;
	}
	return ultrawidelock_freertos_kv_set(s->key, value, val_len);
}
