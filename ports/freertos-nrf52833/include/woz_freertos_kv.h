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
#ifndef WOZ_FREERTOS_KV_H
#define WOZ_FREERTOS_KV_H

#include <stddef.h>
#include <stdint.h>

/*
 * Key ranges. The two consumers must not collide, and a range is cheaper to
 * reason about than a registry.
 */
#define WOZ_KV_KEY_ALIRO_PROV 0x0001u
/* OpenThread's own settings keys are 0x0000..0x00ff; they are offset into this
 * range so they cannot land on an Aliro key. */
#define WOZ_KV_KEY_OPENTHREAD_BASE 0x1000u
#define WOZ_KV_KEY_OPENTHREAD_LIMIT 0x1100u
/*
 * Matter's own records, above OpenThread's window. Only the shared Thread
 * transport's SRP host-name suffix lives here so far; it is deliberately NOT
 * under a tree the factory reset clears, because the SRP client's ECDSA key
 * survives that too and the two have to be erased together or not at all.
 */
#define WOZ_KV_KEY_MATTER_BASE 0x2000u
#define WOZ_KV_KEY_MATTER_SRP_HOST_ID 0x2000u
/*
 * The operational identity, one record per settings path the fabric store
 * writes. Numbered explicitly rather than hashed: a hash could alias two
 * records, and the set is small and fixed.
 *
 * WOZ_KV_KEY_MATTER_FAB_OK is written LAST and erased FIRST by the store, which
 * is what makes a half-written identity detectable rather than merely unlikely.
 */
#define WOZ_KV_KEY_MATTER_FAB_VER 0x2010u
#define WOZ_KV_KEY_MATTER_FAB_OK 0x2011u
#define WOZ_KV_KEY_MATTER_FAB_TD 0x2012u
#define WOZ_KV_KEY_MATTER_FAB_XP 0x2013u
#define WOZ_KV_KEY_MATTER_FAB_ICLEN 0x2014u
#define WOZ_KV_KEY_MATTER_FAB_ICAC 0x2015u
/* One per fabric slot; MATTER_SUPPORTED_FABRICS is 3, the window holds 16. */
#define WOZ_KV_KEY_MATTER_FAB_SLOT0 0x2020u
#define WOZ_KV_KEY_MATTER_FAB_SLOT_LIMIT 0x2030u
#define WOZ_KV_KEY_MATTER_LIMIT 0x2100u

/*
 * PSA Internal Trusted Storage, which exists for exactly one reason: OpenThread
 * signs its SRP registrations with an ECDSA key that must survive a reboot, and
 * routing that through PSA key references is what keeps Mbed TLS's PK, ECP and
 * BIGNUM modules out of the image.
 *
 * A directory record plus a fixed set of slots. The directory maps the 64-bit
 * PSA uid onto a slot, because a uid cannot be hashed into 16 bits without the
 * possibility of aliasing two keys -- and two aliased keys is a node that signs
 * with the wrong one.
 */
#define WOZ_KV_KEY_PSA_ITS_DIR 0x3000u
#define WOZ_KV_KEY_PSA_ITS_SLOT0 0x3001u
#define WOZ_KV_KEY_PSA_ITS_SLOTS 8u
#define WOZ_KV_KEY_PSA_ITS_LIMIT 0x3100u

/* A key no record can carry: erased flash reads as all ones. */
#define WOZ_KV_KEY_NONE 0xffffu

/* The largest single value. Sized by the Aliro provisioning blob, the biggest
 * thing either consumer stores. */
#define WOZ_KV_VALUE_MAX 768u

enum woz_kv_result {
	WOZ_KV_OK = 0,
	/* No record for that key. Not an error; the caller decides. */
	WOZ_KV_NOT_FOUND = -1,
	/* The key or length is outside what this store accepts. */
	WOZ_KV_INVALID = -2,
	/* The live set no longer fits one page, even after compaction. */
	WOZ_KV_FULL = -3,
	/* The flash refused a read, write, or erase. */
	WOZ_KV_IO = -4,
	/* Both pages are unreadable or unformatted and could not be recovered. */
	WOZ_KV_CORRUPT = -5,
};

/**
 * Mount the store, formatting it if no valid page is found.
 *
 * Safe to call more than once; later calls are no-ops. Returns WOZ_KV_OK or a
 * negative woz_kv_result.
 */
int woz_freertos_kv_init(void);

/**
 * Read a value. On WOZ_KV_OK, *length carries the stored length; on entry it
 * carries the capacity of the buffer. A value larger than the buffer is
 * refused with WOZ_KV_INVALID and *length set to the stored length, so a
 * caller can size a second attempt.
 *
 * value may be NULL with *length zero to ask only for the stored length.
 */
int woz_freertos_kv_get(uint16_t key, void *value, size_t *length);

/** Write a value, replacing any earlier one for that key. */
int woz_freertos_kv_set(uint16_t key, const void *value, size_t length);

/** Forget one key. WOZ_KV_NOT_FOUND if it was not stored. */
int woz_freertos_kv_delete(uint16_t key);

/**
 * Forget everything, leaving a formatted store.
 *
 * This is a factory reset of what this port owns. It is not an erase of the
 * flash region: a caller that wants only its own keys gone must delete them.
 */
int woz_freertos_kv_erase_all(void);

/** Bytes of the active page still available, for headroom reporting. */
size_t woz_freertos_kv_free_bytes(void);

#endif /* WOZ_FREERTOS_KV_H */
