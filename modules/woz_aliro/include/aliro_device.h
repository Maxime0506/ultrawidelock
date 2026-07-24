// Aliro initiator (User-Device) session layer: the device-side counterpart of
// aliro_reader.c. Drives the credential-auth handshake from the phone/fob role —
// parses the reader's AUTH0/AUTH1/EXCHANGE commands, runs the mirror-image key
// schedule (ECDH, the two ECDSA transcripts, the §8.3.1.13 salt), and produces
// the sealed responses. The result is the same 32-byte URSK the reader derives.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_device — the initiator half of the Aliro Access Protocol. Reuses the
 * direction-symmetric crypto (aliro_crypto.c) and EC primitives (aliro_prim.h);
 * the only genuinely new logic is the inverse codec (aliro_device_apdu) and the
 * device view of the two AES-256-GCM channels (opposite seal/open direction to
 * the reader). Compiled by the device build + host tests only.
 *
 * Provenance: clean-room, mirrored from the reader flow in aliro_reader.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "aliro_device_apdu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Device view of an Aliro AES-256-GCM channel. The reader's aliro_secchan seals
 * on direction 0 and opens on direction 1; the device is the mirror — it OPENS
 * reader->device traffic (direction 0, key s0) and SEALS device->reader traffic
 * (direction 1, key s1). Both per-direction counters start at 1 (§8.3.1.13). */
struct aliro_dev_secchan {
	uint8_t s0[32]; /* reader->device key (block split S0) */
	uint8_t s1[32]; /* device->reader key (block split S1) */
	uint32_t ctr_r2d;
	uint32_t ctr_d2r;
};

void aliro_dev_secchan_init(struct aliro_dev_secchan *sc, const uint8_t s0[32],
			    const uint8_t s1[32]);
/* Open an inbound reader->device message (direction 0, key s0). Advances ctr_r2d
 * on success; returns <0 on a GCM tag mismatch (hard auth failure). */
int aliro_dev_secchan_open(struct aliro_dev_secchan *sc, const uint8_t *ct, size_t ct_len,
			   const uint8_t tag[16], uint8_t *pt);
/* Seal an outbound device->reader message (direction 1, key s1). Advances
 * ctr_d2r; writes ct (ct_len == pt_len) and the 16-byte tag. Returns 0. */
int aliro_dev_secchan_seal(struct aliro_dev_secchan *sc, const uint8_t *pt, size_t pt_len,
			   uint8_t *ct, uint8_t tag[16]);

/* Seal an AUTH0 fast-phase cryptogram (§8.3.1.11): the byte-exact mirror of the
 * reader's aliro_crypto_verify_cryptogram. AES-256-GCM under CryptogramSK, a
 * 12-byte all-zero IV, no AAD; out = encrypted_payload(plain_len) || 16-byte tag.
 * out must hold plain_len + 16 bytes. Returns 0 on success. */
int aliro_dev_seal_cryptogram(const uint8_t cryptogram_sk[32], const uint8_t *plain,
			      size_t plain_len, uint8_t *out);

/* Standard-path session derivation, factored EC-free (the caller supplies the
 * ECDH shared X). Builds z, the §8.3.1.13 SESSION salt and the 160-byte block,
 * then the device AP channel (s0/s1 = block split S0/S1) and the URSK. Every
 * input mirrors the reader's on_auth1_response. Returns 0 on success. */
int aliro_device_derive_session(const uint8_t shared_x[32], const uint8_t txid[16],
				const uint8_t reader_group_x[32], const uint8_t reader_eph_x[32],
				const uint8_t reader_id[32], uint8_t exp_phase, const uint8_t *a5,
				size_t a5n, const uint8_t device_eph_x[32],
				struct aliro_dev_secchan *sc, uint8_t ursk[32]);

/* ---- full initiator state machine (uses EC via aliro_prim) ---- */

enum aliro_device_phase {
	ALIRO_DEV_IDLE = 0,
	ALIRO_DEV_SENT_AUTH0_RESP,
	ALIRO_DEV_SENT_AUTH1_RESP,
	ALIRO_DEV_ESTABLISHED,
	ALIRO_DEV_FAILED,
};

struct aliro_device {
	/* identity + the reader this device is provisioned to talk to */
	uint8_t cred_priv[32];        /* Access Credential private scalar */
	uint8_t cred_pub[65];         /* = pub(cred_priv), presented in AUTH1Response */
	uint8_t reader_id[32];        /* expected reader identifier */
	uint8_t reader_verif_pub[65]; /* reader group verification key */
	uint8_t reader_group_x[32];   /* = reader_verif_pub.x, salt field 1 */

	/* per-transaction */
	uint8_t dev_eph_priv[32];
	uint8_t dev_eph_pub[65];
	uint8_t reader_eph_pub[65];
	uint8_t txid[16];
	uint8_t exp_phase;
	uint16_t version;
	uint8_t ursk[32];
	struct aliro_dev_secchan sc;

	const uint8_t *a5; /* 0xA5 proprietary-info TLV for the salt (CSA v1.0 default) */
	size_t a5n;

	enum aliro_device_phase phase;
};

/* Initialise a device: derive cred_pub from cred_priv, latch the expected reader
 * identity + verification key (reader_group_x = its X), set the CSA v1.0 default
 * 0xA5 salt TLV. Returns 0 on success, <0 if the EC public-key recovery fails. */
int aliro_device_init(struct aliro_device *d, const uint8_t cred_priv[32],
		      const uint8_t reader_id[32], const uint8_t reader_verif_pub[65]);

/* Feed one inbound Access-Protocol command payload (the bytes inside the BLE
 * envelope: an ISO7816 APDU) and produce the response payload (<TLV|ct||tag> SW).
 * Advances d->phase. Returns 0 on success, <0 on any parse/crypto/auth failure
 * (d->phase set to ALIRO_DEV_FAILED). */
int aliro_device_on_command(struct aliro_device *d, const uint8_t *ap_payload, size_t len,
			    uint8_t *resp, size_t cap, size_t *resp_len);

#ifdef __cplusplus
}
#endif
