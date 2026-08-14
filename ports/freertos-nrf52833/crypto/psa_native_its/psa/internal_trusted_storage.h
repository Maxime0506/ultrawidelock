/*
 * The PSA Internal Trusted Storage API, as this port implements it.
 *
 * psa_crypto_storage.c reaches for this header whenever MBEDTLS_PSA_ITS_FILE_C
 * is absent -- the "native ITS implementation" branch -- and that is the
 * supported way to supply a backend Mbed TLS does not ship. The implementation
 * is crypto/psa_its_freertos.c, over the port's key-value store.
 *
 * Only the four entry points and the types they need. This is not a general PSA
 * Storage header: psa_ps_* (protected storage) is a different API with a
 * different threat model and nothing in this image asks for it.
 *
 * The directory holding this file is on the include path AHEAD of Mbed TLS's
 * own, and contains exactly these two headers. Every other <psa/...> include
 * still resolves to Mbed TLS, because there is nothing here to shadow them
 * with.
 */
#ifndef WOZ_PSA_INTERNAL_TRUSTED_STORAGE_H
#define WOZ_PSA_INTERNAL_TRUSTED_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include <psa/crypto_types.h>
#include <psa/crypto_values.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSA_ITS_API_VERSION_MAJOR 1
#define PSA_ITS_API_VERSION_MINOR 0

typedef uint64_t psa_storage_uid_t;
typedef uint32_t psa_storage_create_flags_t;

#define PSA_STORAGE_FLAG_NONE 0u
/** The object may not be modified or removed once written. */
#define PSA_STORAGE_FLAG_WRITE_ONCE (1u << 0)

struct psa_storage_info_t {
	uint32_t size;
	psa_storage_create_flags_t flags;
};

/**
 * Create or replace @p uid.
 *
 * @return PSA_SUCCESS, PSA_ERROR_NOT_PERMITTED for a WRITE_ONCE object that
 *         already exists, PSA_ERROR_INSUFFICIENT_STORAGE when the object does
 *         not fit or no slot is free, PSA_ERROR_STORAGE_FAILURE on a write that
 *         did not land.
 */
psa_status_t psa_its_set(psa_storage_uid_t uid, uint32_t data_length, const void *p_data,
			 psa_storage_create_flags_t create_flags);

/**
 * Read @p data_length bytes from @p data_offset within @p uid.
 *
 * @return PSA_SUCCESS, PSA_ERROR_DOES_NOT_EXIST, PSA_ERROR_INVALID_ARGUMENT for
 *         a range outside the object, PSA_ERROR_STORAGE_FAILURE.
 */
psa_status_t psa_its_get(psa_storage_uid_t uid, uint32_t data_offset, uint32_t data_length,
			 void *p_data, size_t *p_data_length);

/** Size and flags of @p uid, without reading it. */
psa_status_t psa_its_get_info(psa_storage_uid_t uid, struct psa_storage_info_t *p_info);

/** Remove @p uid. PSA_ERROR_NOT_PERMITTED if it was created WRITE_ONCE. */
psa_status_t psa_its_remove(psa_storage_uid_t uid);

#ifdef __cplusplus
}
#endif

#endif /* WOZ_PSA_INTERNAL_TRUSTED_STORAGE_H */
