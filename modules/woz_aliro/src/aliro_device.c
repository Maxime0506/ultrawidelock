/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Aliro initiator (User-Device) session layer. See aliro_device.h. Mirrors the
 * reader flow in aliro_reader.c: the reader builds AUTH0/AUTH1/EXCHANGE commands
 * and verifies the device; here the device parses them and proves itself, running
 * the same §8.3.1.13 key schedule to the same URSK.
 */
#include "aliro_device.h"

#include <string.h>

#include "aliro_apdu.h"   /* aliro_apdu_build_authdata, ALIRO_AUTH_*, INS/TAG */
#include "aliro_crypto.h" /* z / salt / block / split / gcm nonce */
#include "aliro_prim.h"   /* EC + AES-GCM primitives */

/* Protocol v1.0 only, matching the reader's ALIRO_VERSION. Feeds the salt as the
 * protocol_version field (§8.3.1.13), which the reader hardcodes to 0x0100. */
#define ALIRO_DEV_VERSION 0x0100u

/* CSA-app, protocol v1.0-only 0xA5 proprietary-info TLV (Aliro Table 10-2), the
 * salt's trailing field. Identical to the reader's k_a5_csa_v1 fallback; a live
 * device sends its own in the op-0x05 Initiate-Access-Protocol message. */
static const uint8_t k_a5_csa_v1[] = {
	0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00,
};

/* ---- device AES-256-GCM channel (mirror direction of aliro_secchan) ---- */

void aliro_dev_secchan_init(struct aliro_dev_secchan *sc, const uint8_t s0[32],
			    const uint8_t s1[32])
{
	memcpy(sc->s0, s0, 32);
	memcpy(sc->s1, s1, 32);
	sc->ctr_r2d = 1; /* §8.3.1.13: both directions start at 1 */
	sc->ctr_d2r = 1;
}

int aliro_dev_secchan_open(struct aliro_dev_secchan *sc, const uint8_t *ct, size_t ct_len,
			   const uint8_t tag[16], uint8_t *pt)
{
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(0, sc->ctr_r2d, nonce); /* direction 0 = reader->device */
	if (aliro_aes256_gcm_decrypt(sc->s0, nonce, ALIRO_GCM_NONCE_LEN, NULL, 0, ct, ct_len, tag,
				     ALIRO_GCM_TAG_LEN, pt) != 0) {
		return -1;
	}
	sc->ctr_r2d++;
	return 0;
}

int aliro_dev_secchan_seal(struct aliro_dev_secchan *sc, const uint8_t *pt, size_t pt_len,
			   uint8_t *ct, uint8_t tag[16])
{
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(1, sc->ctr_d2r, nonce); /* direction 1 = device->reader */
	if (aliro_aes256_gcm_encrypt(sc->s1, nonce, ALIRO_GCM_NONCE_LEN, NULL, 0, pt, pt_len, ct,
				     tag, ALIRO_GCM_TAG_LEN) != 0) {
		return -1;
	}
	sc->ctr_d2r++;
	return 0;
}

int aliro_dev_seal_cryptogram(const uint8_t cryptogram_sk[32], const uint8_t *plain,
			      size_t plain_len, uint8_t *out)
{
	uint8_t iv[ALIRO_GCM_NONCE_LEN] = {0}; /* §8.3.1.11: 12-byte all-zero IV, no AAD */

	return aliro_aes256_gcm_encrypt(cryptogram_sk, iv, ALIRO_GCM_NONCE_LEN, NULL, 0, plain,
					plain_len, out, out + plain_len, ALIRO_GCM_TAG_LEN);
}

int aliro_device_derive_session(const uint8_t shared_x[32], const uint8_t txid[16],
				const uint8_t reader_group_x[32], const uint8_t reader_eph_x[32],
				const uint8_t reader_id[32], uint8_t exp_phase, const uint8_t *a5,
				size_t a5n, const uint8_t device_eph_x[32],
				struct aliro_dev_secchan *sc, uint8_t ursk[32])
{
	uint8_t z[32], salt[ALIRO_SALT_MAX], block[ALIRO_KEY_BLOCK_LEN];
	uint8_t enc[ALIRO_SESSION_KEY_LEN], dec[ALIRO_SESSION_KEY_LEN];
	size_t slen;

	aliro_crypto_derive_z(shared_x, txid, z);
	if (aliro_salt_build(ALIRO_SALT_SESSION, txid, reader_group_x, reader_eph_x, reader_id,
			     ALIRO_IFACE_BLE, ALIRO_DEV_VERSION, exp_phase, 0x01u, NULL, a5, a5n,
			     salt, &slen) != 0) {
		return -1;
	}
	if (aliro_crypto_derive_block(z, salt, slen, device_eph_x, block) != 0) {
		return -1;
	}
	/* with_c=1: enc=S0 (reader->device), dec=S1 (device->reader), URSK=S4. The
	 * device channel opens on s0 and seals on s1 (mirror of the reader). */
	aliro_crypto_split(block, 1, enc, dec, ursk);
	aliro_dev_secchan_init(sc, enc, dec);
	return 0;
}

/* ---- full initiator state machine (EC via aliro_prim) ---- */

int aliro_device_init(struct aliro_device *d, const uint8_t cred_priv[32],
		      const uint8_t reader_id[32], const uint8_t reader_verif_pub[65])
{
	memset(d, 0, sizeof(*d));
	memcpy(d->cred_priv, cred_priv, 32);
	if (aliro_ec_p256_pub_from_priv(d->cred_priv, d->cred_pub) != 0) {
		return -1;
	}
	memcpy(d->reader_id, reader_id, 32);
	memcpy(d->reader_verif_pub, reader_verif_pub, 65);
	memcpy(d->reader_group_x, reader_verif_pub + 1, 32);
	d->a5 = k_a5_csa_v1;
	d->a5n = sizeof(k_a5_csa_v1);
	d->version = ALIRO_DEV_VERSION;
	d->phase = ALIRO_DEV_IDLE;
	return 0;
}

int aliro_device_on_command(struct aliro_device *d, const uint8_t *ap_payload, size_t len,
			    uint8_t *resp, size_t cap, size_t *resp_len)
{
	uint8_t ins;
	const uint8_t *data;
	size_t dlen;

	if (aliro_apdu_unwrap(ap_payload, len, &ins, &data, &dlen) != 0) {
		goto fail;
	}

	switch (ins) {
	case ALIRO_INS_AUTH0: {
		struct aliro_auth0_command c;

		if (aliro_dev_parse_auth0_cmd(data, dlen, &c) != 0) {
			goto fail;
		}
		if (memcmp(c.reader_id, d->reader_id, 32) != 0) {
			goto fail; /* not the reader we are provisioned to */
		}
		memcpy(d->reader_eph_pub, c.reader_eph_pub, 65);
		memcpy(d->txid, c.txid, 16);
		d->exp_phase = c.exp_phase;
		if (aliro_ec_p256_keygen(d->dev_eph_priv, d->dev_eph_pub) != 0) {
			goto fail;
		}

		size_t n = 0;

		if (aliro_dev_build_auth0_resp(d->dev_eph_pub, NULL, resp, cap, &n) != 0 ||
		    aliro_apdu_append_sw(resp, &n, cap, 0x9000u) != 0) {
			goto fail;
		}
		*resp_len = n;
		d->phase = ALIRO_DEV_SENT_AUTH0_RESP;
		return 0;
	}
	case ALIRO_INS_AUTH1: {
		struct aliro_auth1_command c;
		uint8_t td[160], shared[ALIRO_SHARED_SECRET_LEN], sig[ALIRO_P256_SIG];
		uint8_t plain[160], tag[ALIRO_GCM_TAG_LEN];
		size_t tn, pl;

		if (aliro_dev_parse_auth1_cmd(data, dlen, &c) != 0) {
			goto fail;
		}
		/* Verify the reader over the reader-usage transcript (device pubX, reader-eph
		 * pubX) with the provisioned reader verification key. */
		if (aliro_apdu_build_authdata(ALIRO_AUTH_READER, d->reader_id, d->dev_eph_pub + 1,
					      d->reader_eph_pub + 1, d->txid, td, sizeof(td),
					      &tn) != 0 ||
		    aliro_ecdsa_p256_verify(d->reader_verif_pub, td, tn, c.reader_sig) != 0) {
			goto fail;
		}
		/* ECDH + the mirror-image session derivation -> URSK + AP channel. */
		if (aliro_ecdh_p256(d->dev_eph_priv, d->reader_eph_pub, shared) != 0 ||
		    aliro_device_derive_session(shared, d->txid, d->reader_group_x,
						d->reader_eph_pub + 1, d->reader_id, d->exp_phase,
						d->a5, d->a5n, d->dev_eph_pub + 1, &d->sc,
						d->ursk) != 0) {
			goto fail;
		}
		/* Sign the device-usage transcript with the credential key, then build +
		 * seal the AUTH1Response (device signature + presented credential key). */
		if (aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, d->reader_id, d->dev_eph_pub + 1,
					      d->reader_eph_pub + 1, d->txid, td, sizeof(td),
					      &tn) != 0 ||
		    aliro_ecdsa_p256_sign(d->cred_priv, td, tn, sig) != 0 ||
		    aliro_dev_build_auth1_resp(sig, d->cred_pub, plain, sizeof(plain), &pl) != 0) {
			goto fail;
		}
		if (cap < pl + ALIRO_GCM_TAG_LEN + 2u ||
		    aliro_dev_secchan_seal(&d->sc, plain, pl, resp, tag) != 0) {
			goto fail;
		}
		memcpy(resp + pl, tag, ALIRO_GCM_TAG_LEN);

		size_t n = pl + ALIRO_GCM_TAG_LEN;

		if (aliro_apdu_append_sw(resp, &n, cap, 0x9000u) != 0) {
			goto fail;
		}
		*resp_len = n;
		d->phase = ALIRO_DEV_SENT_AUTH1_RESP;
		return 0;
	}
	case ALIRO_INS_EXCHANGE: {
		uint8_t pt[64], plain[8], tag[ALIRO_GCM_TAG_LEN];
		struct aliro_exchange_command ec;
		size_t pl;

		if (dlen < ALIRO_GCM_TAG_LEN) {
			goto fail;
		}

		size_t ctlen = dlen - ALIRO_GCM_TAG_LEN;

		if (ctlen > sizeof(pt) ||
		    aliro_dev_secchan_open(&d->sc, data, ctlen, data + ctlen, pt) != 0) {
			goto fail;
		}
		aliro_dev_parse_exchange_cmd(pt, ctlen, &ec); /* URSK-ready trigger */

		if (aliro_dev_build_exchange_resp(0x0000u, plain, sizeof(plain), &pl) != 0) {
			goto fail;
		}
		if (cap < pl + ALIRO_GCM_TAG_LEN + 2u ||
		    aliro_dev_secchan_seal(&d->sc, plain, pl, resp, tag) != 0) {
			goto fail;
		}
		memcpy(resp + pl, tag, ALIRO_GCM_TAG_LEN);

		size_t n = pl + ALIRO_GCM_TAG_LEN;

		if (aliro_apdu_append_sw(resp, &n, cap, 0x9000u) != 0) {
			goto fail;
		}
		*resp_len = n;
		d->phase = ALIRO_DEV_ESTABLISHED;
		return 0;
	}
	default:
		break;
	}

fail:
	d->phase = ALIRO_DEV_FAILED;
	return -1;
}
