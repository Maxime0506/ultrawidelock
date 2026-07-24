#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct woz_aliro_access_document {
	uint8_t device_public_key[65];
	const uint8_t *data_element;
	size_t data_element_length;
	const uint8_t *issuer_signed_item;
	size_t issuer_signed_item_length;
	uint64_t digest_id;
	const uint8_t *expected_digest;
	const uint8_t *cose_protected;
	size_t cose_protected_length;
	const uint8_t *cose_payload;
	size_t cose_payload_length;
	const uint8_t *cose_signature;
	const uint8_t *issuer_kid;
	size_t issuer_kid_length;
	const uint8_t *issuer_certificate;
	size_t issuer_certificate_length;
	uint8_t signed_timestamp[20];
	uint8_t valid_from[20];
	uint8_t valid_until[20];
	bool time_verification_required;
	bool has_validity_iteration;
	uint64_t validity_iteration;
};

/* Strictly parse the compact-key Aliro Access Document subset used by a reader. */
int woz_aliro_parse_access_document(const uint8_t *device_response, size_t device_response_length,
				    const uint8_t *requested_element,
				    size_t requested_element_length,
				    struct woz_aliro_access_document *result);

#ifdef __cplusplus
}
#endif
