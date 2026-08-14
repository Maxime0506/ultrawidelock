/*
 * The port's persistent key-value store.
 *
 * Two consumers need one on this part: the reader's provisioning blob and
 * OpenThread's settings. Both want small named values that survive a reset and
 * a firmware update, which is what this provides, on the same two flash pages
 * the Zephyr oracle reserves for its settings partition.
 *
 * Values are addressed by a 16-bit key. Keys are namespaced by their consumer
 * rather than by a string path, because a path costs flash on every write and
 * neither consumer has more than a handful of keys.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_KV_H
#define ULTRAWIDELOCK_FREERTOS_KV_H

#include <stddef.h>
#include <stdint.h>

/*
 * Key ranges. The two consumers must not collide, and a range is cheaper to
 * reason about than a registry.
 */
#define ULTRAWIDELOCK_KV_KEY_CRED_PROV 0x0001u
/* OpenThread's own settings keys are 0x0000..0x00ff; they are offset into this
 * range so they cannot land on a credential key. */
#define ULTRAWIDELOCK_KV_KEY_OPENTHREAD_BASE 0x1000u
#define ULTRAWIDELOCK_KV_KEY_OPENTHREAD_LIMIT 0x1100u
/*
 * Matter's own records, above OpenThread's window. Only the shared Thread
 * transport's SRP host-name suffix lives here so far; it is deliberately NOT
 * under a tree the factory reset clears, because the SRP client's ECDSA key
 * survives that too and the two have to be erased together or not at all.
 */
#define ULTRAWIDELOCK_KV_KEY_MATTER_BASE 0x2000u
#define ULTRAWIDELOCK_KV_KEY_MATTER_SRP_HOST_ID 0x2000u
/*
 * The operational identity, one record per settings path the fabric store
 * writes. Numbered explicitly rather than hashed: a hash could alias two
 * records, and the set is small and fixed.
 *
 * ULTRAWIDELOCK_KV_KEY_MATTER_FAB_OK is written LAST and erased FIRST by the
 * store, which is what makes a half-written identity detectable rather than
 * merely unlikely.
 */
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_VER 0x2010u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_OK 0x2011u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_TD 0x2012u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_XP 0x2013u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_ICLEN 0x2014u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_ICAC 0x2015u
/* One per fabric slot; MATTER_SUPPORTED_FABRICS is 3, the window holds 16. */
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_SLOT0 0x2020u
#define ULTRAWIDELOCK_KV_KEY_MATTER_FAB_SLOT_LIMIT 0x2030u
#define ULTRAWIDELOCK_KV_KEY_MATTER_LIMIT 0x2100u

/* A key no record can carry: erased flash reads as all ones. */
#define ULTRAWIDELOCK_KV_KEY_NONE 0xffffu

/* The largest single value. Sized by the credential provisioning blob, the biggest
 * thing either consumer stores. */
#define ULTRAWIDELOCK_KV_VALUE_MAX 768u

enum ultrawidelock_kv_result {
	ULTRAWIDELOCK_KV_OK = 0,
	/* No record for that key. Not an error; the caller decides. */
	ULTRAWIDELOCK_KV_NOT_FOUND = -1,
	/* The key or length is outside what this store accepts. */
	ULTRAWIDELOCK_KV_INVALID = -2,
	/* The live set no longer fits one page, even after compaction. */
	ULTRAWIDELOCK_KV_FULL = -3,
	/* The flash refused a read, write, or erase. */
	ULTRAWIDELOCK_KV_IO = -4,
	/* Both pages are unreadable or unformatted and could not be recovered. */
	ULTRAWIDELOCK_KV_CORRUPT = -5,
};

/**
 * Mount the store, formatting it if no valid page is found.
 *
 * Safe to call more than once; later calls are no-ops. Returns ULTRAWIDELOCK_KV_OK or a
 * negative ultrawidelock_kv_result.
 */
int ultrawidelock_freertos_kv_init(void);

/**
 * Read a value. On ULTRAWIDELOCK_KV_OK, *length carries the stored length; on entry it
 * carries the capacity of the buffer. A value larger than the buffer is
 * refused with ULTRAWIDELOCK_KV_INVALID and *length set to the stored length, so a
 * caller can size a second attempt.
 *
 * value may be NULL with *length zero to ask only for the stored length.
 */
int ultrawidelock_freertos_kv_get(uint16_t key, void *value, size_t *length);

/** Write a value, replacing any earlier one for that key. */
int ultrawidelock_freertos_kv_set(uint16_t key, const void *value, size_t length);

/** Forget one key. ULTRAWIDELOCK_KV_NOT_FOUND if it was not stored. */
int ultrawidelock_freertos_kv_delete(uint16_t key);

/**
 * Forget everything, leaving a formatted store.
 *
 * This is a factory reset of what this port owns. It is not an erase of the
 * flash region: a caller that wants only its own keys gone must delete them.
 */
int ultrawidelock_freertos_kv_erase_all(void);

/** Bytes of the active page still available, for headroom reporting. */
size_t ultrawidelock_freertos_kv_free_bytes(void);

#endif /* ULTRAWIDELOCK_FREERTOS_KV_H */
