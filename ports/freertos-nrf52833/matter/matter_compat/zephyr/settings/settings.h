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

#include <stdbool.h>
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
int settings_delete(const char *name);

/**
 * Registered handler, called once per stored record under its subtree.
 *
 * @param name the path BELOW the subtree, e.g. "f0" for "mfab/f0".
 */
typedef int (*settings_set_cb)(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg);

struct settings_handler_static {
	const char *name;
	settings_set_cb h_set;
};

/*
 * Zephyr puts these in a linker section and walks it. This port has one
 * registrant -- the Matter fabric store -- so the "section" is a single slot,
 * claimed at definition time. A second registrant is a build error rather than
 * a silent overwrite: see the assertion in the .c file.
 */
void woz_settings_register(const struct settings_handler_static *h);

#define SETTINGS_STATIC_HANDLER_DEFINE(sym, tree, get_fn, set_fn, commit_fn, export_fn)            \
	static const struct settings_handler_static woz_settings_##sym = {                         \
		.name = (tree),                                                                    \
		.h_set = (set_fn),                                                                 \
	};                                                                                         \
	__attribute__((constructor)) static void woz_settings_reg_##sym(void)                      \
	{                                                                                          \
		woz_settings_register(&woz_settings_##sym);                                        \
	}

/** Walk every stored record under @p subtree, calling the registered handler. */
int settings_load_subtree(const char *subtree);

/**
 * Zephyr's name matcher: true when @p name's first component equals @p key, with
 * @p next left pointing at whatever follows it (NULL when nothing does).
 */
bool settings_name_steq(const char *name, const char *key, const char **next);

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H */
