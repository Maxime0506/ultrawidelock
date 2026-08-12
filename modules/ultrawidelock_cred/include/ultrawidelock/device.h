// credential initiator (User-Device) session layer: the device-side counterpart of
// ultrawidelock_reader.c. Drives the credential-auth handshake from the phone/fob role:
// parses the reader's AUTH0/AUTH1/EXCHANGE commands, runs the mirror-image key
// schedule (ECDH, the two ECDSA transcripts, the §8.3.1.13 salt), and produces
// the sealed responses. The result is the same 32-byte URSK the reader derives.
/*
 * ultrawidelock_device: the initiator half of the credential Access Protocol. Reuses the
 * direction-symmetric crypto (ultrawidelock_crypto.c) and EC primitives (ultrawidelock_prim.h);
 * the only genuinely new logic is the inverse codec (ultrawidelock_device_apdu) and the
 * device view of the two AES-256-GCM channels (opposite seal/open direction to
 * the reader). Compiled by the device build + host tests only. Mirrors the
 * reader flow in ultrawidelock_reader.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ultrawidelock_crypto.h" /* ULTRAWIDELOCK_KEY_BLOCK_LEN + channel/BleSK derivation */
#include "ultrawidelock_device_apdu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Device view of an credential AES-256-GCM channel. The reader's ultrawidelock_secchan seals
 * on direction 0 and opens on direction 1; the device is the mirror: it OPENS
 * reader->device traffic (direction 0, key s0) and SEALS device->reader traffic
 * (direction 1, key s1). Both per-direction counters start at 1 (§8.3.1.13). */
struct ultrawidelock_dev_secchan {
	uint8_t s0[32]; /* reader->device key (block split S0) */
	uint8_t s1[32]; /* device->reader key (block split S1) */
	uint32_t ctr_r2d;
	uint32_t ctr_d2r;
};

void ultrawidelock_dev_secchan_init(struct ultrawidelock_dev_secchan *sc, const uint8_t s0[32],
			    const uint8_t s1[32]);
/* Open an inbound reader->device message (direction 0, key s0). Advances ctr_r2d
 * on success; returns <0 on a GCM tag mismatch (hard auth failure). */
int ultrawidelock_dev_secchan_open(struct ultrawidelock_dev_secchan *sc, const uint8_t *ct,
				   size_t ct_len, const uint8_t tag[16], uint8_t *pt);
/* Seal an outbound device->reader message (direction 1, key s1). Advances
 * ctr_d2r; writes ct (ct_len == pt_len) and the 16-byte tag. Returns 0. */
int ultrawidelock_dev_secchan_seal(struct ultrawidelock_dev_secchan *sc, const uint8_t *pt,
				   size_t pt_len, uint8_t *ct, uint8_t tag[16]);

/* ---- device BleSK ranging channel (mirror of the reader's sc_ble) ----
 *
 * The UWB ranging-setup traffic (Reader-Status AP-Completed, Initiate-Ranging,
 * M1-M4, notifications) rides one BleSK-keyed AES-256-GCM channel, using the same
 * construction as the AP channel above, but each SDU carries a 4-byte
 * [proto][id][len_be16] header that is authenticated as GCM AAD and whose wire
 * length field is payload+16 (the reader's ultrawidelock_msg_seal/open framing). We
 * reuse struct ultrawidelock_dev_secchan: s0 = BleSKReader (device OPENS, direction 0),
 * s1 = BleSKDevice (device SEALS, direction 1). */

/* Initialise the device BleSK channel from the 160-byte key block and the
 * versions salt (reader_supported_versions || selected_version; 01 00 01 00 for
 * v1.0-only). Derives BleSKReader/BleSKDevice via ultrawidelock_crypto_derive_ble_keys;
 * both counters start at 1. Returns 0, or <0 if the HKDF derivation fails. */
int ultrawidelock_dev_blesk_init(struct ultrawidelock_dev_secchan *ch,
				 const uint8_t block[ULTRAWIDELOCK_KEY_BLOCK_LEN],
				 const uint8_t *versions_salt, size_t salt_len);

/* Open a reader-sealed BleSK SDU: wire = [proto][id][len_be16][ct||tag] with
 * len_be16 = payload_len + 16. Authenticates the 4-byte header as AAD, opens on
 * direction 0 (key s0 = BleSKReader), writes plain = [proto][id][payload_len_be16]
 * [payload] and sets *plain_len = 4 + payload_len. Advances ctr_r2d; returns 0 on
 * success, <0 on a tag/length failure (hard auth failure). */
int ultrawidelock_dev_ble_open(struct ultrawidelock_dev_secchan *ch, const uint8_t *wire,
			       size_t wire_len, uint8_t *plain, size_t plain_cap,
			       size_t *plain_len);

/* Seal a device->reader BleSK SDU (inverse of ultrawidelock_dev_ble_open): plain =
 * [proto][id][payload_len_be16][payload] (the header length must equal the
 * payload length); writes wire = [proto][id][(payload_len+16)_be16][ct||tag] on
 * direction 1 (key s1 = BleSKDevice), header as AAD, and sets *wire_len. Advances
 * ctr_d2r; returns 0 or <0. Byte-compatible with the reader's ultrawidelock_msg_open. */
int ultrawidelock_dev_ble_seal(struct ultrawidelock_dev_secchan *ch, const uint8_t *plain,
			       size_t plain_len, uint8_t *wire, size_t wire_cap, size_t *wire_len);

/* Seal an AUTH0 fast-phase cryptogram (§8.3.1.11): the byte-exact mirror of the
 * reader's ultrawidelock_crypto_verify_cryptogram. AES-256-GCM under CryptogramSK, a
 * 12-byte all-zero IV, no AAD; out = encrypted_payload(plain_len) || 16-byte tag.
 * out must hold plain_len + 16 bytes. Returns 0 on success. */
int ultrawidelock_dev_seal_cryptogram(const uint8_t cryptogram_sk[32], const uint8_t *plain,
			      size_t plain_len, uint8_t *out);

/* Standard-path session derivation, factored EC-free (the caller supplies the
 * ECDH shared X). Builds z, the §8.3.1.13 SESSION salt and the 160-byte block,
 * then the device AP channel (s0/s1 = block split S0/S1) and the URSK. Every
 * input mirrors the reader's on_auth1_response. block_out, if non-NULL, receives
 * the 160-byte block (the BleSK at offset 96 lives there and nowhere else, so a
 * caller that needs the ranging channel must ask for it). Returns 0 on success. */
int ultrawidelock_device_derive_session(const uint8_t shared_x[32], const uint8_t txid[16],
				const uint8_t reader_group_x[32], const uint8_t reader_eph_x[32],
				const uint8_t reader_id[32], uint8_t exp_phase, const uint8_t *a5,
				size_t a5n, const uint8_t device_eph_x[32],
				struct ultrawidelock_dev_secchan *sc, uint8_t ursk[32],
				uint8_t block_out[ULTRAWIDELOCK_KEY_BLOCK_LEN]);

/* ---- full initiator state machine (uses EC via ultrawidelock_prim) ---- */

/* Cap on the BleSK salt: reader_supported_versions || selected_version, and the
 * readers bound their advertised list at 8 (ULTRAWIDELOCK_MAX_VERSIONS, ultrawidelock_ble.c), so
 * 2 * (8 + 1) bytes is the most §11.8.1 can ask for. */
#define ULTRAWIDELOCK_DEV_BLESK_SALT_MAX 18u

enum ultrawidelock_device_phase {
	ULTRAWIDELOCK_DEV_IDLE = 0,
	ULTRAWIDELOCK_DEV_SENT_AUTH0_RESP,
	ULTRAWIDELOCK_DEV_SENT_AUTH1_RESP,
	ULTRAWIDELOCK_DEV_ESTABLISHED,
	ULTRAWIDELOCK_DEV_FAILED,
};

/**
 * credential device: access credential (private scalar and public point), reader identity and
 * verification key, per-transaction ephemeral and channel state, BLE-SK salt, and transaction
 * phase.
 */
struct ultrawidelock_device {
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
	struct ultrawidelock_dev_secchan sc;     /* Access-Protocol channel (S0/S1), from AUTH1 */
	struct ultrawidelock_dev_secchan sc_ble; /* BleSK ranging channel, from the same block */

	/* §11.8.1 BleSK salt = reader_supported_versions || selected_version. It is a
	 * property of the PEER, not of us: our ESP32 reader publishes {0x0100} alone
	 * (salt 01 00 01 00) while the nRF publishes {0x0100, 0x0009} (salt
	 * 01 00 00 09 01 00, measured on air 2026-07-25), so it cannot be a constant.
	 * ultrawidelock_device_init installs the single-version v1.0 default; any transport
	 * that has really read the peer's GATT list MUST override it via
	 * ultrawidelock_device_set_blesk_salt, or the ranging channel derives a key the peer
	 * does not share and the first sealed SDU fails as a GCM tag mismatch. */
	uint8_t blesk_salt[ULTRAWIDELOCK_DEV_BLESK_SALT_MAX];
	size_t blesk_salt_len;

	const uint8_t *a5; /* 0xA5 proprietary-info TLV for the salt (CSA v1.0 default) */
	size_t a5n;

	enum ultrawidelock_device_phase phase;
};

/* Initialise a device: derive cred_pub from cred_priv, latch the expected reader
 * identity + verification key (reader_group_x = its X), set the CSA v1.0 default
 * 0xA5 salt TLV. Returns 0 on success, <0 if the EC public-key recovery fails. */
int ultrawidelock_device_init(struct ultrawidelock_device *d, const uint8_t cred_priv[32],
		      const uint8_t reader_id[32], const uint8_t reader_verif_pub[65]);

/* Install the peer's real BleSK salt, overriding the v1.0 default. Build it with
 * ultrawidelock_ble_central_blesk_salt from the versions the GATT reader-SPSM READ
 * actually returned. Must be called before AUTH1, which is where the ranging
 * channel is derived. Returns 0, or -1 on an empty/odd/oversized salt. */
int ultrawidelock_device_set_blesk_salt(struct ultrawidelock_device *d, const uint8_t *salt,
					size_t len);

/* Feed one inbound Access-Protocol command payload (the bytes inside the BLE
 * envelope: an ISO7816 APDU) and produce the response payload (<TLV|ct||tag> SW).
 * Advances d->phase. Returns 0 on success, <0 on any parse/crypto/auth failure
 * (d->phase set to ULTRAWIDELOCK_DEV_FAILED). */
int ultrawidelock_device_on_command(struct ultrawidelock_device *d, const uint8_t *ap_payload,
				    size_t len, uint8_t *resp, size_t cap, size_t *resp_len);

#ifdef __cplusplus
}
#endif
