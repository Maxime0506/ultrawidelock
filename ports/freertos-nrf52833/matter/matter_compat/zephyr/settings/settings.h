/*
 * Zephyr's settings API, narrowed to what the shared Matter Thread transport
 * uses: initialise, load one subtree directly, save one value.
 *
 * Zephyr's settings are a string-keyed tree. This port's store is a flat
 * uint16_t keyspace, so the translation is a fixed table in
 * matter_settings_freertos.c rather than anything general. That is the point:
 * an unknown path is a build-time-unknown record, and this backend refuses it
 * loudly instead of reporting "nothing stored", which is what a tree-shaped
 * backend would say and is indistinguishable from a first boot.
 */
#ifndef WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H
#define WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H

#include <stddef.h>
#include <sys/types.h>

/**
 * Hands the stored bytes to the caller. Returns the number read, negative on
 * error, matching Zephyr.
 */
typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);

/**
 * Called once per matching record. @p key is the path below the subtree, which
 * is empty here because every path this backend knows is a leaf.
 */
typedef int (*settings_load_direct_cb)(const char *key, size_t len, settings_read_cb read_cb,
				       void *cb_arg, void *param);

int settings_subsys_init(void);
int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb, void *param);
int settings_save_one(const char *name, const void *value, size_t val_len);

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H */
