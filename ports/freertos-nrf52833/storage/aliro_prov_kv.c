/*
 * aliro_prov (FreeRTOS key-value backend) — the standalone twin of the Zephyr
 * port's aliro_prov_settings.c and the ESP32 port's aliro_prov_nvs.c. The
 * serialisation, the dev fallback and all of the trust logic live in the
 * portable aliro_prov.c; this file only moves that one blob in and out of the
 * port's key-value store under WOZ_KV_KEY_ALIRO_PROV.
 */
#include <string.h>

#include <woz_freertos_kv.h>
#include <woz_freertos_platform.h>

#include "aliro_prov.h"

#define PROV_TAG "aliro_prov"

/*
 * The store has to be able to hold the largest blob the serialiser can produce,
 * and that size moves with ALIRO_TRUST_MAX. Catch a raise of that limit here,
 * at build time, rather than at the first provisioning write on a customer's
 * board.
 */
_Static_assert(ALIRO_PROV_BLOB_MAX <= WOZ_KV_VALUE_MAX,
	       "the provisioning blob no longer fits one key-value record");

/*
 * STATIC, not on the stack. ALIRO_PROV_BLOB_MAX is 700 bytes and struct
 * aliro_trust_store is another 778, which together overrun the port's default
 * 4096-byte task stacks once the reader's own frames are counted. The Zephyr
 * twin learned this as an MPU fault through the bottom of the main stack a few
 * seconds into boot.
 *
 * One buffer serves both directions because no caller does both at once: every
 * path into here comes from the reader's provisioning code, which serialises on
 * its own lock, and provisioning writes are rare besides.
 */
static uint8_t s_blob[ALIRO_PROV_BLOB_MAX];

/**
 * Load identity and trust anchors from the key-value store.
 *
 * 0 a stored blob was loaded; 1 nothing was stored and the dev default is in
 * place; -1 the store failed or held a malformed blob, and the dev default is
 * in place. The identity is always usable on return.
 */
int aliro_prov_load(struct aliro_reader_identity *id, struct aliro_trust_store *ts)
{
	size_t len = sizeof(s_blob);
	int rc = woz_freertos_kv_init();

	if (rc != WOZ_KV_OK) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG,
				 "kv init rc=%d; using DEV identity", rc);
		aliro_prov_dev_default(id, ts);
		return -1;
	}

	rc = woz_freertos_kv_get(WOZ_KV_KEY_ALIRO_PROV, s_blob, &len);
	if (rc == WOZ_KV_NOT_FOUND) {
		/* Never provisioned. Not an error. */
		aliro_prov_dev_default(id, ts);
		return 1;
	}
	if (rc != WOZ_KV_OK) {
		/*
		 * WOZ_KV_INVALID reaches here too, and it means the stored
		 * record is longer than any blob this firmware can produce. It
		 * is treated as malformed rather than retried with a bigger
		 * buffer: nothing that writes this key can exceed the static
		 * assert above, so a longer record is corruption or a future
		 * format, and neither is safe to parse.
		 */
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG,
				 "kv get rc=%d; using DEV identity", rc);
		aliro_prov_dev_default(id, ts);
		return -1;
	}

	if (aliro_prov_deserialize(s_blob, len, id, ts) != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG,
				 "stored blob malformed (%u B); using DEV identity",
				 (unsigned)len);
		aliro_prov_dev_default(id, ts);
		return -1;
	}
	return 0;
}

/**
 * Persist identity and trust anchors. 0 on success, negative on a store error
 * or on a blob that will not serialise.
 */
int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)
{
	size_t len = 0;
	int rc = woz_freertos_kv_init();

	if (rc != WOZ_KV_OK) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG, "kv init rc=%d", rc);
		return rc;
	}
	if (aliro_prov_serialize(id, ts, s_blob, sizeof(s_blob), &len) != 0) {
		return -1;
	}

	rc = woz_freertos_kv_set(WOZ_KV_KEY_ALIRO_PROV, s_blob, len);
	if (rc != WOZ_KV_OK) {
		woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG, "kv set rc=%d", rc);
		return rc;
	}
	woz_freertos_log(WOZ_FREERTOS_LOG_INFO, PROV_TAG, "provisioning persisted (%u B, %u trusted)",
			 (unsigned)len, ts != NULL ? ts->count : 0u);
	return 0;
}

/**
 * Forget the stored identity and every trust anchor.
 *
 * This deletes one key and not the store, because OpenThread's settings live in
 * the same two pages and its SRP client key must outlive a factory reset: the
 * SRP host name is the factory EUI-64, name ownership on the border router is
 * first-come-first-served by key, and a new key asking for the same name is
 * refused until the old lease expires, up to 14 days of being attached to
 * Thread but unreachable on it.
 */
int aliro_prov_erase(void)
{
	int rc = woz_freertos_kv_init();

	if (rc != WOZ_KV_OK) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, PROV_TAG,
				 "kv init rc=%d; nothing erased", rc);
		return rc;
	}

	rc = woz_freertos_kv_delete(WOZ_KV_KEY_ALIRO_PROV);
	if (rc == WOZ_KV_NOT_FOUND) {
		/* Already absent. A reset that had nothing to undo succeeded. */
		rc = WOZ_KV_OK;
	}
	/*
	 * The rc is reported, not swallowed. A factory reset that quietly did
	 * nothing is worse than one that fails loudly: the board comes back
	 * looking reset, pairs, and then rejects the phone with the old anchors
	 * still in the store.
	 */
	woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, PROV_TAG, "factory reset: erased prov (rc=%d)",
			 rc);
	return rc;
}
