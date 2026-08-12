#ifndef ULTRAWIDELOCK_PIV_APDU_H
#define ULTRAWIDELOCK_PIV_APDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIV_APDU_MAX_RESPONSE 1024u
#define PIV_PIN_BYTES 8u
#define PIV_P256_HASH_BYTES 32u
#define PIV_P256_POINT_BYTES 65u
#define PIV_P256_SHARED_SECRET_BYTES 32u
#define PIV_P256_RAW_SIGNATURE_BYTES 64u
#define PIV_KEY_REF_AUTH 0x9au
#define PIV_KEY_REF_KEY_MANAGEMENT 0x9du

/*
 * Platform operations for the persistent PIV identity.
 *
 * PIN callbacks return 0 on success, 1 on a wrong PIN, -2 when the PIN is
 * blocked or has not been provisioned, and -1 on an internal failure. The
 * retry count is returned for ISO 7816 status 63Cx.
 *
 * get_certificate selects slot 9A or 9D using key_ref. sign_hash receives the
 * off-card SHA-256 digest required by PIV GENERAL AUTHENTICATE. derive_shared
 * performs the P-256 ECDH primitive for slot 9D.
 */
struct piv_apdu_backend {
	int (*get_certificate)(void *ctx, uint8_t key_ref,
			       const uint8_t **certificate,
			       size_t *certificate_len);
	int (*get_guid)(void *ctx, uint8_t guid[16]);
	int (*pin_status)(void *ctx, uint8_t *retries);
	int (*verify_pin)(void *ctx, const uint8_t pin[PIV_PIN_BYTES],
			  uint8_t *retries);
	int (*change_pin)(void *ctx,
			  const uint8_t old_pin[PIV_PIN_BYTES],
			  const uint8_t new_pin[PIV_PIN_BYTES],
			  uint8_t *retries);
	int (*sign_hash)(void *ctx,
			 const uint8_t hash[PIV_P256_HASH_BYTES],
			 uint8_t signature[PIV_P256_RAW_SIGNATURE_BYTES]);
	int (*derive_shared)(
		void *ctx,
		const uint8_t peer_public_key[PIV_P256_POINT_BYTES],
		uint8_t shared_secret[PIV_P256_SHARED_SECRET_BYTES]);
};

/**
 * PIV APDU command handler state: tracks APDU selection, PIN verification status, and buffered
 * response for chunked transmission.
 */
struct piv_apdu {
	bool selected;
	bool pin_verified;
	bool pin_required;
	const struct piv_apdu_backend *backend;
	void *backend_ctx;
	uint8_t pending[PIV_APDU_MAX_RESPONSE];
	size_t pending_offset;
	size_t pending_len;
};

void piv_apdu_init(struct piv_apdu *piv,
		   const struct piv_apdu_backend *backend, void *backend_ctx,
		   bool pin_required);
void piv_apdu_reset(struct piv_apdu *piv);

/* Handle one short ISO 7816 command APDU and always include a status word. */
int piv_apdu_transmit(struct piv_apdu *piv,
		      const uint8_t *command, size_t command_len,
		      uint8_t *response, size_t response_cap,
		      size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif
