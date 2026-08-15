/* SPDX-License-Identifier: ISC */

/*
 * credential BLE advertising primitives.
 *
 * Kept as portable C so the byte-order rules can be tested on the host using
 * the specification's published known-answer vectors.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	ULTRAWIDELOCK_CRED_BLE_ADDRESS_SIZE = 6,
	ULTRAWIDELOCK_CRED_DYNAMIC_TAG_INPUT_SIZE = 16,
	ULTRAWIDELOCK_CRED_DYNAMIC_TAG_SIZE = 7,
};

/* address_le uses Zephyr/bt_addr_t storage order. The generated AES input is
 * 6 zero pad bytes || AdvA most-significant-byte first || expiry big-endian. */
void ultrawidelock_cred_dynamic_tag_input(
	const uint8_t address_le[ULTRAWIDELOCK_CRED_BLE_ADDRESS_SIZE], uint32_t expiry_timestamp,
	uint8_t plaintext[ULTRAWIDELOCK_CRED_DYNAMIC_TAG_INPUT_SIZE]);

/* The dynamic tag is the seven most-significant AES ciphertext octets. */
void ultrawidelock_cred_dynamic_tag_from_ciphertext(
	const uint8_t ciphertext[ULTRAWIDELOCK_CRED_DYNAMIC_TAG_INPUT_SIZE],
	uint8_t tag[ULTRAWIDELOCK_CRED_DYNAMIC_TAG_SIZE]);

#ifdef __cplusplus
}
#endif
