/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host test for the Aliro reader engine (aliro_reader.c): the AUTH0 -> AUTH1 ->
 * EXCHANGE -> AP-Completed transaction, driven end-to-end by a scripted phone.
 *
 * The phone side is NOT the reader's own code path run twice: it re-derives the
 * whole §8.3.1.13 key schedule independently, from the bytes the reader put on
 * the wire (AUTH0's ephemeral key, txid, reader_id, ExpeditedPhaseType) plus the
 * out-of-band secrets a real phone would hold (the credential keypair and the
 * reader's verification key). Every GCM open that succeeds — in either
 * direction — is therefore a cross-check that both independent derivations of
 * the session keys, BleSK channel, and URSK agree byte-for-byte.
 *
 * The EC primitive layer is the deterministic stand-in in aliro_prim_host.c
 * (NOT P-256); everything above it — the state machine, wire codec, HKDF
 * schedule, AES-GCM channels, trust gate, Kpersistent lifecycle — is the real
 * shared-core code. The BLE transport, ranging adapter and NVS backend are
 * recording doubles defined here.
 *
 * Scenarios, in one linear script (the engine's state is process-global):
 *   T0  dev-identity walk-up: unknown credential accepted (dev-open policy),
 *       ranging armed, no Kpersistent minted, nothing persisted
 *   A   provisioned identity + trusted credential: full standard phase,
 *       ranging SDU relay, unlock/relock notify, Kpersistent minted+persisted
 *   B   expedited-fast walk-up off the stored Kpersistent (no AUTH1)
 *   C   failures: untrusted credential, trust-last/store-failure, GeneralError,
 *       corrupted AUTH1Response, bad device signature, malformed AUTH0Response,
 *       session-table exhaustion
 *   D   trust_clear, adv refresh with a provisioned GRK, attach-mode start,
 *       provision_clear back to the dev identity
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aliro_apdu.h"
#include "aliro_ble.h"
#include "aliro_crypto.h"
#include "aliro_lab.h"
#include "aliro_prim.h"
#include "aliro_prov.h"
#include "aliro_ranging.h"
#include "aliro_reader.h"

/* Failure injection into the prim double (aliro_prim_host.c). Every hook
 * defaults off and disarms itself after firing, so the walk-ups before
 * section E run against the plain fakes. */
extern int aliro_prim_host_fail_init;
extern int aliro_prim_host_fail_keygen;
extern int aliro_prim_host_fail_sign;
extern int aliro_prim_host_fail_pub_from_priv;
extern int aliro_prim_host_fail_random_after;  /* -1 off; N: fail after N successes */
extern int aliro_prim_host_fail_encrypt_after; /* -1 off; N: fail after N successes */

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* ---- aliro_ble transport double ----------------------------------------- */

static struct aliro_ble_config s_cfg;
static bool s_ble_started;

#define TX_MAX 32
static struct {
	uint8_t b[512];
	size_t n;
} s_tx[TX_MAX];
static int s_txn, s_tx_rd;

static int s_adv_sets;
static uint8_t s_adv_grk[16];
static int s_readv;

int aliro_ble_start(const struct aliro_ble_config *cfg)
{
	s_cfg = *cfg;
	s_ble_started = true;
	return 0;
}

static bool s_ble_prepare_fail;

int aliro_ble_prepare(const struct aliro_ble_config *cfg)
{
	if (s_ble_prepare_fail) {
		return -1;
	}
	s_cfg = *cfg;
	return 0;
}

static const int s_svc_dummy;

const struct ble_gatt_svc_def *aliro_ble_service_def(void)
{
	return (const struct ble_gatt_svc_def *)&s_svc_dummy;
}

int aliro_ble_start_attached(void)
{
	s_ble_started = true;
	return 0;
}

uint16_t aliro_ble_spsm(void)
{
	return 0x0080;
}

int aliro_ble_send(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	(void)conn_handle;
	if (s_txn < TX_MAX && len <= sizeof(s_tx[0].b)) {
		memcpy(s_tx[s_txn].b, data, len);
		s_tx[s_txn].n = len;
		s_txn++;
	}
	return 0;
}

void aliro_ble_post_reader_status(void (*cb)(bool unsecured), bool unsecured)
{
	cb(unsecured); /* the double runs "the host task" inline */
}

void aliro_ble_set_adv_params(const uint8_t group_id8[8], const uint8_t sub_id2[2],
			      const uint8_t grk[16], int8_t tx_power)
{
	(void)group_id8;
	(void)sub_id2;
	(void)tx_power;
	memcpy(s_adv_grk, grk, 16);
	s_adv_sets++;
}

void aliro_ble_readvertise(void)
{
	s_readv++;
}

/* Pop the next reader-transmitted SDU, or NULL when the queue is drained. */
static const uint8_t *tx_next(size_t *n)
{
	if (s_tx_rd >= s_txn) {
		return NULL;
	}
	*n = s_tx[s_tx_rd].n;
	return s_tx[s_tx_rd++].b;
}

static int tx_pending(void)
{
	return s_txn - s_tx_rd;
}

/* Reclaim the recording queue (TX_MAX slots for the whole run). Only call at a
 * scenario boundary where every previously sent frame has been consumed. */
static void tx_reset(void)
{
	s_txn = 0;
	s_tx_rd = 0;
}

/* ---- aliro_ranging double ------------------------------------------------ */

static int s_rng_inits, s_rng_starts, s_rng_stops, s_rng_feeds;
static uint32_t s_rng_sid;
static uint8_t s_rng_ursk[ALIRO_URSK_LEN];
static uint8_t s_rng_feed_buf[64];
static size_t s_rng_feed_len;

/* One-shot failure switches for section E (self-clearing, default off). */
static int s_rng_init_fail, s_rng_start_fail;

int aliro_ranging_init(void)
{
	s_rng_inits++;
	if (s_rng_init_fail) {
		s_rng_init_fail = 0;
		return -1;
	}
	return 0;
}

int aliro_ranging_start(uint16_t conn_handle, uint32_t session_id, const uint8_t *ursk,
			struct aliro_secchan *sc_ble)
{
	(void)conn_handle;
	(void)sc_ble;
	if (s_rng_start_fail) {
		s_rng_start_fail = 0;
		return -1;
	}
	s_rng_sid = session_id;
	memcpy(s_rng_ursk, ursk, ALIRO_URSK_LEN);
	s_rng_starts++;
	return 0;
}

int aliro_ranging_feed(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	(void)conn_handle;
	if (len <= sizeof(s_rng_feed_buf)) {
		memcpy(s_rng_feed_buf, data, len);
		s_rng_feed_len = len;
	}
	s_rng_feeds++;
	return 0;
}

void aliro_ranging_stop(uint16_t conn_handle)
{
	(void)conn_handle;
	s_rng_stops++;
}

/* ---- aliro_prov NVS double (RAM blob via the real serializer) ------------ */

static uint8_t s_nvs[ALIRO_PROV_BLOB_MAX];
static size_t s_nvs_len;
static bool s_nvs_has;
static bool s_nvs_fail;
static int s_nvs_stores;

int aliro_prov_load(struct aliro_reader_identity *id, struct aliro_trust_store *ts)
{
	if (s_nvs_has && aliro_prov_deserialize(s_nvs, s_nvs_len, id, ts) == 0) {
		return 0;
	}
	aliro_prov_dev_default(id, ts);
	return 1;
}

int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)
{
	if (s_nvs_fail) {
		return -1;
	}
	if (aliro_prov_serialize(id, ts, s_nvs, sizeof(s_nvs), &s_nvs_len) != 0) {
		return -1;
	}
	s_nvs_has = true;
	s_nvs_stores++;
	return 0;
}

/* ---- the scripted phone -------------------------------------------------- */

/* Fallback 0xA5 TLV the reader salts with when the phone's op-0x05 carried
 * none; must match k_a5_csa_v1 in aliro_reader.c. */
static const uint8_t k_a5_csa[] = {
	0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00,
};

/* The phone's own 0xA5 proprietary-info TLV for the with-a5 walk-ups. */
static const uint8_t k_a5_phone[] = {0xa5, 0x04, 0xde, 0xad, 0xbe, 0xef};

/* BleSK derivation salt: reader_supported_versions || selected version, both
 * v1.0 — must match init_ble_channel() in aliro_reader.c. */
static const uint8_t k_ble_salt[] = {0x01, 0x00, 0x01, 0x00};

static const uint8_t k_ap_completed[] = {0x02, 0x03, 0x00, 0x04, 0x00, 0x02, 0x20, 0x00};

struct ph {
	/* out-of-band secrets a provisioned phone holds */
	uint8_t cred_priv[32];
	uint8_t cred_pub[65];
	uint8_t rvk[65]; /* reader verification key = pub(sign_priv) */

	/* per-walk-up state */
	uint8_t eph_priv[32];
	uint8_t eph_pub[65];
	const uint8_t *a5;
	size_t a5n;

	/* captured off the reader's AUTH0 */
	uint8_t r_eph_pub[65];
	uint8_t txid[16];
	uint8_t r_id[32];
	uint8_t exp_phase;

	/* independently derived schedule */
	uint8_t z[32];
	uint8_t block[ALIRO_KEY_BLOCK_LEN];
	uint8_t ursk[ALIRO_URSK_LEN];
	uint8_t kp[ALIRO_KPERSISTENT_LEN];

	/* AP secure channel, phone's view: r2p = reader-seal direction (0),
	 * p2r = phone-seal direction (1); counters start at 1 (§8.3.1). */
	uint8_t k_r2p[32], k_p2r[32];
	uint32_t r2p_ctr, p2r_ctr;

	/* BleSK ranging channel, phone's view. */
	uint8_t ble_r2p[32], ble_p2r[32];
	uint32_t ble_r2p_ctr, ble_p2r_ctr;
};

/* Deliver one enveloped SDU to the reader as the transport would. */
static void ph_send(uint16_t conn, uint8_t type, uint8_t op, const uint8_t *pl, size_t n)
{
	uint8_t f[600];

	f[0] = type;
	f[1] = op;
	f[2] = (uint8_t)(n >> 8);
	f[3] = (uint8_t)n;
	memcpy(f + 4, pl, n);
	s_cfg.cb.on_data(conn, f, (uint16_t)(4 + n));
}

/* Split a captured ACCESS command frame into INS + APDU body. */
static int parse_cmd(const uint8_t *f, size_t n, uint8_t *ins, const uint8_t **body, size_t *blen)
{
	if (n < 4 || f[0] != ALIRO_PROTO_ACCESS || f[1] != ALIRO_AP_OP_COMMAND) {
		return -1;
	}
	size_t alen = ((size_t)f[2] << 8) | f[3];
	const uint8_t *a = f + 4;

	if (alen < 6 || 4 + alen > n) {
		return -1;
	}
	*ins = a[1];
	size_t lc = a[4];

	if (5 + lc > alen) {
		return -1;
	}
	*body = a + 5;
	*blen = lc;
	return 0;
}

/* One short-form TLV into out; returns bytes written. */
static size_t tlv1(uint8_t *out, uint8_t tag, const uint8_t *v, size_t n)
{
	out[0] = tag;
	out[1] = (uint8_t)n;
	memcpy(out + 2, v, n);
	return 2 + n;
}

/* Phone-direction AEAD: the phone seals with direction-1 nonces (what the
 * reader opens) and opens direction-0 nonces (what the reader seals) — the
 * mirror image of aliro_secchan_seal/open, driven off the raw primitives. */
static int ph_seal(const uint8_t key[32], uint32_t *ctr, const uint8_t *aad, size_t aadn,
		   const uint8_t *pt, size_t ptn, uint8_t *ct, uint8_t tag[16])
{
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(1, *ctr, nonce);
	if (aliro_aes256_gcm_encrypt(key, nonce, sizeof(nonce), aad, aadn, pt, ptn, ct, tag, 16) !=
	    0) {
		return -1;
	}
	(*ctr)++;
	return 0;
}

static int ph_open(const uint8_t key[32], uint32_t *ctr, const uint8_t *aad, size_t aadn,
		   const uint8_t *ct, size_t ctn, const uint8_t *tag, uint8_t *pt)
{
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(0, *ctr, nonce);
	if (aliro_aes256_gcm_decrypt(key, nonce, sizeof(nonce), aad, aadn, ct, ctn, tag, 16, pt) !=
	    0) {
		return -1;
	}
	(*ctr)++;
	return 0;
}

/* Open one reader-sealed BleSK wire SDU ([proto][id][(len+16)be16][ct||tag])
 * into its plaintext form, mirroring aliro_msg_open. */
static int ph_open_ble(struct ph *p, const uint8_t *w, size_t wl, uint8_t *plain, size_t *plen)
{
	if (wl < 4 + 16) {
		return -1;
	}
	size_t clen = ((size_t)w[2] << 8) | w[3];

	if (clen < 16 || 4 + clen > wl) {
		return -1;
	}
	size_t payn = clen - 16;
	uint8_t aad[4] = {w[0], w[1], (uint8_t)(payn >> 8), (uint8_t)payn};
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(0, p->ble_r2p_ctr, nonce);
	if (aliro_aes256_gcm_decrypt(p->ble_r2p, nonce, sizeof(nonce), aad, sizeof(aad), w + 4,
				     payn, w + 4 + payn, 16, plain + 4) != 0) {
		return -1;
	}
	p->ble_r2p_ctr++;
	memcpy(plain, aad, 4);
	*plen = 4 + payn;
	return 0;
}

/* Seal one phone->reader BleSK SDU from its plaintext form ([proto][id]
 * [len_be16][payload]), mirroring aliro_msg_seal from the device side. */
static size_t ph_seal_ble(struct ph *p, const uint8_t *plain, size_t plen, uint8_t *wire)
{
	size_t payn = plen - 4;
	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	memcpy(wire, plain, 2);
	wire[2] = (uint8_t)((payn + 16) >> 8);
	wire[3] = (uint8_t)(payn + 16);
	aliro_crypto_gcm_nonce(1, p->ble_p2r_ctr, nonce);
	if (aliro_aes256_gcm_encrypt(p->ble_p2r, nonce, sizeof(nonce), plain, 4, plain + 4, payn,
				     wire + 4, wire + 4 + payn, 16) != 0) {
		return 0;
	}
	p->ble_p2r_ctr++;
	return 4 + payn + 16;
}

/* Phone opens the connection: op-0x05 Initiate-Access-Protocol, optionally
 * carrying its 0xA5 proprietary-info TLV. */
static void ph_initiate(struct ph *p, uint16_t conn, int with_a5)
{
	uint8_t pl[32];
	size_t n = 0;

	pl[n++] = 0x00; /* leading non-A5 byte: capture must scan, not assume offset 0 */
	if (with_a5) {
		memcpy(pl + n, k_a5_phone, sizeof(k_a5_phone));
		n += sizeof(k_a5_phone);
		p->a5 = k_a5_phone;
		p->a5n = sizeof(k_a5_phone);
	} else {
		p->a5 = k_a5_csa;
		p->a5n = sizeof(k_a5_csa);
	}
	ph_send(conn, ALIRO_PROTO_NOTIFICATION, ALIRO_NOTIF_INITIATE_AP, pl, n);
}

/* Consume the reader's AUTH0 and capture the transcript inputs off the wire.
 * Returns 0 and fills r_eph_pub/txid/r_id/exp_phase, or -1. */
static int ph_take_auth0(struct ph *p)
{
	size_t n;
	const uint8_t *f = tx_next(&n);
	const uint8_t *body, *v;
	size_t blen, vl;
	uint8_t ins;

	if (f == NULL || parse_cmd(f, n, &ins, &body, &blen) != 0 || ins != ALIRO_INS_AUTH0) {
		return -1;
	}
	if (aliro_tlv_find(body, blen, ALIRO_TAG_READER_EPH, &v, &vl) != 0 || vl != 65) {
		return -1;
	}
	memcpy(p->r_eph_pub, v, 65);
	if (aliro_tlv_find(body, blen, ALIRO_TAG_TXID, &v, &vl) != 0 || vl != 16) {
		return -1;
	}
	memcpy(p->txid, v, 16);
	if (aliro_tlv_find(body, blen, ALIRO_TAG_READER_ID, &v, &vl) != 0 || vl != 32) {
		return -1;
	}
	memcpy(p->r_id, v, 32);
	if (aliro_tlv_find(body, blen, ALIRO_TAG_EXP_PHASE, &v, &vl) != 0 || vl != 1) {
		return -1;
	}
	p->exp_phase = v[0];
	return 0;
}

/* Standard-phase AUTH0Response: fresh ephemeral key, no cryptogram. Also runs
 * the phone's half of the ECDH so z is ready for the AUTH1 derivations. */
static void ph_auth0_resp(struct ph *p, uint16_t conn, uint8_t eph_seed)
{
	uint8_t pl[80], shared[32];
	size_t n;

	memset(p->eph_priv, eph_seed, sizeof(p->eph_priv));
	aliro_ec_p256_pub_from_priv(p->eph_priv, p->eph_pub);
	aliro_ecdh_p256(p->eph_priv, p->r_eph_pub, shared);
	aliro_crypto_derive_z(shared, p->txid, p->z);

	n = tlv1(pl, ALIRO_TAG_DEVICE_PUBX, p->eph_pub, 65);
	pl[n++] = 0x90;
	pl[n++] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, n);
}

/* The phone's independent run of the standard-phase key schedule: session salt
 * -> 160-byte block -> AP channel keys + URSK + BleSK channel + the Kpersistent
 * this transaction would mint. Everything from wire captures + p->z. */
static int ph_derive_standard(struct ph *p)
{
	uint8_t salt[ALIRO_SALT_MAX], enc[32], dec[32];
	size_t sl;

	if (aliro_salt_build(ALIRO_SALT_SESSION, p->txid, p->rvk + 1, p->r_eph_pub + 1, p->r_id,
			     ALIRO_IFACE_BLE, 0x0100, p->exp_phase, 0x01, NULL, p->a5, p->a5n,
			     salt, &sl) != 0 ||
	    aliro_crypto_derive_block(p->z, salt, sl, p->eph_pub + 1, p->block) != 0) {
		return -1;
	}
	aliro_crypto_split(p->block, 1, enc, dec, p->ursk);
	memcpy(p->k_r2p, enc, 32);
	memcpy(p->k_p2r, dec, 32);
	p->r2p_ctr = p->p2r_ctr = 1;
	if (aliro_crypto_derive_ble_keys(p->block, k_ble_salt, sizeof(k_ble_salt), p->ble_r2p,
					 p->ble_p2r) != 0) {
		return -1;
	}
	p->ble_r2p_ctr = p->ble_p2r_ctr = 1;

	if (aliro_salt_build(ALIRO_SALT_KPERSISTENT, p->txid, p->rvk + 1, p->r_eph_pub + 1, p->r_id,
			     ALIRO_IFACE_BLE, 0x0100, p->exp_phase, 0x01, p->cred_pub + 1, p->a5,
			     p->a5n, salt, &sl) != 0 ||
	    aliro_crypto_derive_key32(p->z, salt, sl, p->eph_pub + 1, p->kp) != 0) {
		return -1;
	}
	return 0;
}

/* Consume the reader's AUTH1, verify its signature against the verification
 * key, then send the sealed AUTH1Response. signer NULL = the credential key;
 * a different signer produces a possession-proof failure. corrupt flips a
 * ciphertext byte so the GCM open must fail. Returns -1 on a script error. */
static int ph_auth1_resp(struct ph *p, uint16_t conn, const uint8_t *signer, int corrupt)
{
	size_t n;
	const uint8_t *f = tx_next(&n);
	const uint8_t *body, *v;
	size_t blen, vl;
	uint8_t ins;

	if (f == NULL || parse_cmd(f, n, &ins, &body, &blen) != 0 || ins != ALIRO_INS_AUTH1) {
		return -1;
	}
	if (aliro_tlv_find(body, blen, ALIRO_TAG_SIG, &v, &vl) != 0 || vl != 64) {
		return -1;
	}

	/* the reader must have proven possession of the provisioned signing key */
	uint8_t td[160];
	size_t tn;

	if (aliro_apdu_build_authdata(ALIRO_AUTH_READER, p->r_id, p->eph_pub + 1, p->r_eph_pub + 1,
				      p->txid, td, sizeof(td), &tn) != 0 ||
	    aliro_ecdsa_p256_verify(p->rvk, td, tn, v) != 0) {
		return -1;
	}

	if (ph_derive_standard(p) != 0) {
		return -1;
	}

	/* AUTH1Response plaintext: credential pub + signature over the
	 * device-usage transcript. */
	uint8_t pt[160], sig[64], pl[200];
	size_t ptn = 0;

	if (aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, p->r_id, p->eph_pub + 1, p->r_eph_pub + 1,
				      p->txid, td, sizeof(td), &tn) != 0 ||
	    aliro_ecdsa_p256_sign(signer != NULL ? signer : p->cred_priv, td, tn, sig) != 0) {
		return -1;
	}
	ptn += tlv1(pt + ptn, ALIRO_TAG_DEVICE_PUB, p->cred_pub, 65);
	ptn += tlv1(pt + ptn, ALIRO_TAG_SIG, sig, 64);

	uint8_t tag[16];

	if (ph_seal(p->k_p2r, &p->p2r_ctr, NULL, 0, pt, ptn, pl, tag) != 0) {
		return -1;
	}
	if (corrupt) {
		pl[0] ^= 0x01;
	}
	memcpy(pl + ptn, tag, 16);
	pl[ptn + 16] = 0x90;
	pl[ptn + 17] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, ptn + 18);
	return 0;
}

/* Consume the reader's sealed EXCHANGE, check the URSK-ready trigger, reply
 * success. Returns -1 on a script error (including a failed open — which would
 * mean the two independent key derivations disagreed). */
static int ph_exchange_resp(struct ph *p, uint16_t conn)
{
	size_t n;
	const uint8_t *f = tx_next(&n);
	const uint8_t *body, *v;
	size_t blen, vl;
	uint8_t ins;

	if (f == NULL || parse_cmd(f, n, &ins, &body, &blen) != 0 || ins != ALIRO_INS_EXCHANGE ||
	    blen < 17) {
		return -1;
	}

	uint8_t pt[64];
	size_t ctn = blen - 16;

	if (ph_open(p->k_r2p, &p->r2p_ctr, NULL, 0, body, ctn, body + ctn, pt) != 0) {
		return -1;
	}
	if (aliro_tlv_find(pt, ctn, ALIRO_TAG_URSK_READY, &v, &vl) != 0) {
		return -1;
	}

	/* success body: len 0x0002, error 0x0000 */
	static const uint8_t ok_body[4] = {0x00, 0x02, 0x00, 0x00};
	uint8_t pl[64], tag[16];

	if (ph_seal(p->k_p2r, &p->p2r_ctr, NULL, 0, ok_body, sizeof(ok_body), pl, tag) != 0) {
		return -1;
	}
	memcpy(pl + sizeof(ok_body), tag, 16);
	pl[sizeof(ok_body) + 16] = 0x90;
	pl[sizeof(ok_body) + 17] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, sizeof(ok_body) + 18);
	return 0;
}

/* Consume + open the reader's AP-Completed off the BleSK channel; returns 0
 * only if it decrypts and matches the §11.1.1 plaintext exactly. */
static int ph_take_ap_completed(struct ph *p)
{
	size_t n, pn;
	const uint8_t *f = tx_next(&n);
	uint8_t plain[64];

	if (f == NULL || f[0] != 0x02 || f[1] != 0x03) {
		return -1;
	}
	if (ph_open_ble(p, f, n, plain, &pn) != 0) {
		return -1;
	}
	return (pn == sizeof(k_ap_completed) && memcmp(plain, k_ap_completed, pn) == 0) ? 0 : -1;
}

/* The ranging session id the device derives: big-endian txid[12..15]. */
static uint32_t ph_sid(const struct ph *p)
{
	return ((uint32_t)p->txid[12] << 24) | ((uint32_t)p->txid[13] << 16) |
	       ((uint32_t)p->txid[14] << 8) | (uint32_t)p->txid[15];
}

/* ---- section-E phone variants (error-path walk-ups) ---------------------- */

/* ph_auth0_resp with a caller-chosen status word: the reader must log the odd
 * SW and continue the standard phase regardless. */
static void ph_auth0_resp_sw2(struct ph *p, uint16_t conn, uint8_t eph_seed, uint8_t sw1,
			      uint8_t sw2)
{
	uint8_t pl[80], shared[32];
	size_t n;

	memset(p->eph_priv, eph_seed, sizeof(p->eph_priv));
	aliro_ec_p256_pub_from_priv(p->eph_priv, p->eph_pub);
	aliro_ecdh_p256(p->eph_priv, p->r_eph_pub, shared);
	aliro_crypto_derive_z(shared, p->txid, p->z);
	n = tlv1(pl, ALIRO_TAG_DEVICE_PUBX, p->eph_pub, 65);
	pl[n++] = sw1;
	pl[n++] = sw2;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, n);
}

/* Fast-phase AUTH0Response off the Kpersistent agreed in the last standard
 * phase (p->kp), mirroring the section-B inline flow: fresh ephemeral,
 * cryptogram under CryptogramSK, fast channel + BleSK keys derived so the
 * script can complete the walk-up if the reader accepts the match. Also runs
 * the ECDH so z is ready if the reader falls back to the standard phase. */
static int ph_auth0_resp_fast(struct ph *p, uint16_t conn, uint8_t eph_seed)
{
	uint8_t salt[ALIRO_SALT_MAX], enc[32], dec[32], fast[ALIRO_KEY_BLOCK_LEN];
	uint8_t crypt[64], payload[48], pl[160], shared[32];
	size_t sl, n = 0;
	static const uint8_t zero_iv[12] = {0};

	memset(p->eph_priv, eph_seed, sizeof(p->eph_priv));
	aliro_ec_p256_pub_from_priv(p->eph_priv, p->eph_pub);
	aliro_ecdh_p256(p->eph_priv, p->r_eph_pub, shared);
	aliro_crypto_derive_z(shared, p->txid, p->z);

	if (aliro_salt_build(ALIRO_SALT_CRYPTOGRAM, p->txid, p->rvk + 1, p->r_eph_pub + 1, p->r_id,
			     ALIRO_IFACE_BLE, 0x0100, p->exp_phase, 0x01, p->cred_pub + 1, p->a5,
			     p->a5n, salt, &sl) != 0 ||
	    aliro_crypto_derive_block(p->kp, salt, sl, p->eph_pub + 1, fast) != 0) {
		return -1;
	}
	memset(payload, 0x22, sizeof(payload));
	if (aliro_aes256_gcm_encrypt(fast + ALIRO_CRYPTOGRAM_SK_OFFSET, zero_iv, sizeof(zero_iv),
				     NULL, 0, payload, sizeof(payload), crypt, crypt + 48,
				     16) != 0) {
		return -1;
	}
	aliro_crypto_split(fast, 0, enc, dec, p->ursk);
	memcpy(p->k_r2p, enc, 32);
	memcpy(p->k_p2r, dec, 32);
	p->r2p_ctr = p->p2r_ctr = 1;
	if (aliro_crypto_derive_ble_keys(fast, k_ble_salt, sizeof(k_ble_salt), p->ble_r2p,
					 p->ble_p2r) != 0) {
		return -1;
	}
	p->ble_r2p_ctr = p->ble_p2r_ctr = 1;

	n += tlv1(pl + n, ALIRO_TAG_DEVICE_PUBX, p->eph_pub, 65);
	n += tlv1(pl + n, 0x9D, crypt, 64);
	pl[n++] = 0x90;
	pl[n++] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, n);
	return 0;
}

/* AUTH0Response carrying a garbage cryptogram: the reader's fast trial must
 * match no stored Kpersistent and fall back to the standard phase. */
static void ph_auth0_resp_badcrypt(struct ph *p, uint16_t conn, uint8_t eph_seed)
{
	uint8_t pl[200], shared[32], junk[64];
	size_t n;

	memset(p->eph_priv, eph_seed, sizeof(p->eph_priv));
	aliro_ec_p256_pub_from_priv(p->eph_priv, p->eph_pub);
	aliro_ecdh_p256(p->eph_priv, p->r_eph_pub, shared);
	aliro_crypto_derive_z(shared, p->txid, p->z);
	memset(junk, 0x6B, sizeof(junk));
	n = tlv1(pl, ALIRO_TAG_DEVICE_PUBX, p->eph_pub, 65);
	n += tlv1(pl + n, 0x9D, junk, 64);
	pl[n++] = 0x90;
	pl[n++] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, n);
}

/* ph_auth1_resp variant: derive the channels as usual, then either seal a
 * well-formed response under a caller-chosen SW (junk_pt=0) or seal a
 * plaintext that cannot parse as an AUTH1Response (junk_pt=1). */
static int ph_auth1_resp_v(struct ph *p, uint16_t conn, int junk_pt, uint8_t sw1, uint8_t sw2)
{
	size_t n;
	const uint8_t *f = tx_next(&n);
	const uint8_t *body;
	size_t blen;
	uint8_t ins;

	if (f == NULL || parse_cmd(f, n, &ins, &body, &blen) != 0 || ins != ALIRO_INS_AUTH1) {
		return -1;
	}
	if (ph_derive_standard(p) != 0) {
		return -1;
	}

	uint8_t td[160], pt[160], sig[64], pl[200], tag[16];
	size_t tn, ptn = 0;

	if (junk_pt) {
		pt[ptn++] = 0xDE;
		pt[ptn++] = 0xAD;
	} else {
		if (aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, p->r_id, p->eph_pub + 1,
					      p->r_eph_pub + 1, p->txid, td, sizeof(td), &tn) != 0 ||
		    aliro_ecdsa_p256_sign(p->cred_priv, td, tn, sig) != 0) {
			return -1;
		}
		ptn += tlv1(pt + ptn, ALIRO_TAG_DEVICE_PUB, p->cred_pub, 65);
		ptn += tlv1(pt + ptn, ALIRO_TAG_SIG, sig, 64);
	}
	if (ph_seal(p->k_p2r, &p->p2r_ctr, NULL, 0, pt, ptn, pl, tag) != 0) {
		return -1;
	}
	memcpy(pl + ptn, tag, 16);
	pl[ptn + 16] = sw1;
	pl[ptn + 17] = sw2;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, ptn + 18);
	return 0;
}

/* Open the reader's EXCHANGE, then reply with a device-side error status: the
 * reader must fail the session instead of completing the AP. */
static int ph_exchange_err(struct ph *p, uint16_t conn)
{
	size_t n;
	const uint8_t *f = tx_next(&n);
	const uint8_t *body;
	size_t blen;
	uint8_t ins;

	if (f == NULL || parse_cmd(f, n, &ins, &body, &blen) != 0 || ins != ALIRO_INS_EXCHANGE ||
	    blen < 17) {
		return -1;
	}

	uint8_t pt[64];
	size_t ctn = blen - 16;

	if (ph_open(p->k_r2p, &p->r2p_ctr, NULL, 0, body, ctn, body + ctn, pt) != 0) {
		return -1;
	}

	static const uint8_t err_body[4] = {0x00, 0x02, 0x00, 0x42};
	uint8_t pl[64], tag[16];

	if (ph_seal(p->k_p2r, &p->p2r_ctr, NULL, 0, err_body, sizeof(err_body), pl, tag) != 0) {
		return -1;
	}
	memcpy(pl + sizeof(err_body), tag, 16);
	pl[sizeof(err_body) + 16] = 0x90;
	pl[sizeof(err_body) + 17] = 0x00;
	ph_send(conn, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, sizeof(err_body) + 18);
	return 0;
}

/* ---- the script ---------------------------------------------------------- */

int main(void)
{
	struct ph p;
	struct aliro_reader_identity dev_id;
	struct aliro_trust_store dev_ts;
	uint8_t out65[65];

	memset(&p, 0, sizeof(p));

	printf("== T0: start + dev-identity walk-up (dev-open trust policy) ==\n");
	/* console/notify paths before anything is provisioned or presented, plus
	 * the compiled-out (CONFIG_WOZ_ALIRO_LAB=n) lab stubs */
	aliro_reader_prov_print(); /* "last cred : (none presented yet)" branch */
	okc("t0.trust_last_nothing", aliro_reader_trust_last() == 1);
	aliro_reader_notify_unlock(true); /* no established session: warn only */
	okc("t0.notify_no_session", tx_pending() == 0);
	aliro_lab_set_enabled(true); /* stub: stays disabled */
	okc("t0.lab_stub_off", !aliro_lab_enabled());
	okc("t0.no_auth_cred_yet", !aliro_reader_authenticated_credential(out65));
	okc("t0.start", aliro_reader_start() == 0);
	okc("t0.transport_up", s_ble_started && s_cfg.cb.on_data != NULL);
	okc("t0.ranging_init", s_rng_inits == 1);

	/* the phone against the dev identity: rvk from the dev signing key */
	aliro_prov_dev_default(&dev_id, &dev_ts);
	aliro_ec_p256_pub_from_priv(dev_id.sign_priv, p.rvk);
	memset(p.cred_priv, 0xC1, sizeof(p.cred_priv));
	aliro_ec_p256_pub_from_priv(p.cred_priv, p.cred_pub);

	s_cfg.cb.on_connected(1);
	ph_initiate(&p, 1, 0); /* no 0xA5 TLV: salts must fall back to CSA v1.0 */
	okc("t0.auth0", ph_take_auth0(&p) == 0);
	okc("t0.auth0.exp_phase_std", p.exp_phase == 0x00); /* no Kpersistent yet */
	ph_auth0_resp(&p, 1, 0xE0);
	okc("t0.auth1_resp", ph_auth1_resp(&p, 1, NULL, 0) == 0);
	okc("t0.exchange", ph_exchange_resp(&p, 1) == 0);
	okc("t0.ap_completed", ph_take_ap_completed(&p) == 0);
	okc("t0.ranging_armed", s_rng_starts == 1);
	okc("t0.ursk_match", memcmp(s_rng_ursk, p.ursk, ALIRO_URSK_LEN) == 0);
	okc("t0.sid_from_txid", s_rng_sid == ph_sid(&p));
	okc("t0.auth_cred_recorded", aliro_reader_authenticated_credential(out65) &&
					     memcmp(out65, p.cred_pub, 65) == 0);
	s_cfg.cb.on_disconnected(1);
	okc("t0.ranging_stopped", s_rng_stops == 1);
	okc("t0.nothing_persisted", s_nvs_stores == 0); /* dev-accepted: no Kpersistent */

	printf("\n== A: provisioned identity + trusted credential (standard) ==\n");
	uint8_t rid[32], sp[32], grk0[16] = {0};

	memset(rid, 0xA0, sizeof(rid));
	memset(sp, 0x33, sizeof(sp));
	okc("a.provision_id", aliro_reader_provision_identity(rid, sp, grk0) == 0);
	okc("a.provision_trust", aliro_reader_provision_add_trust(p.cred_pub) == 0);
	aliro_ec_p256_pub_from_priv(sp, p.rvk); /* new verification key */

	s_cfg.cb.on_connected(2);
	ph_initiate(&p, 2, 1); /* with the phone's own 0xA5 TLV this time */
	okc("a.auth0", ph_take_auth0(&p) == 0);
	okc("a.auth0.rid_provisioned", memcmp(p.r_id, rid, 32) == 0);
	okc("a.auth0.exp_phase_std", p.exp_phase == 0x00);
	ph_auth0_resp(&p, 2, 0xE1);
	okc("a.auth1_resp", ph_auth1_resp(&p, 2, NULL, 0) == 0);
	okc("a.exchange", ph_exchange_resp(&p, 2) == 0);
	okc("a.ap_completed", ph_take_ap_completed(&p) == 0);
	okc("a.ranging_armed", s_rng_starts == 2);
	okc("a.ursk_match", memcmp(s_rng_ursk, p.ursk, ALIRO_URSK_LEN) == 0);

	/* established: a device ranging SDU rides the BleSK channel to the engine */
	{
		uint8_t plain[6] = {0x01, 0x01, 0x00, 0x02, 0xAB, 0xCD};
		uint8_t wire[64];
		size_t wl = ph_seal_ble(&p, plain, sizeof(plain), wire);

		okc("a.sdu_sealed", wl > 0);
		s_cfg.cb.on_data(2, wire, (uint16_t)wl);
		okc("a.sdu_fed_to_engine", s_rng_feeds == 1 && s_rng_feed_len == sizeof(plain) &&
						   memcmp(s_rng_feed_buf, plain, sizeof(plain)) ==
							   0);
	}

	/* unlock grant + relock notifications (Wallet animation trigger) */
	{
		uint8_t plain[64];
		size_t n, pn;
		const uint8_t *f;
		static const uint8_t grant[8] = {0x02, 0x02, 0x00, 0x04, 0x00, 0x02, 0x04, 0x01};
		static const uint8_t relock[8] = {0x02, 0x02, 0x00, 0x04, 0x00, 0x02, 0x04, 0x00};

		aliro_reader_notify_unlock(true);
		f = tx_next(&n);
		okc("a.grant_sent", f != NULL && ph_open_ble(&p, f, n, plain, &pn) == 0 &&
					   pn == 8 && memcmp(plain, grant, 8) == 0);
		aliro_reader_notify_unlock(false);
		f = tx_next(&n);
		okc("a.relock_sent", f != NULL && ph_open_ble(&p, f, n, plain, &pn) == 0 &&
					     pn == 8 && memcmp(plain, relock, 8) == 0);
	}

	/* disconnect persists the minted Kpersistent — compare against the
	 * phone's independent derivation of it */
	s_cfg.cb.on_disconnected(2);
	okc("a.kp_persisted", s_nvs_stores == 3); /* 2 provision calls + this one */
	{
		struct aliro_reader_identity id2;
		struct aliro_trust_store ts2;

		okc("a.kp_blob_loads", aliro_prov_load(&id2, &ts2) == 0);
		okc("a.kp_valid_bit", (ts2.kp_valid & 1u) != 0);
		okc("a.kp_match", memcmp(ts2.kpersistent[0], p.kp, 32) == 0);
	}

	printf("\n== B: expedited-fast walk-up off the stored Kpersistent ==\n");
	s_cfg.cb.on_connected(3);
	ph_initiate(&p, 3, 1);
	okc("b.auth0", ph_take_auth0(&p) == 0);
	okc("b.auth0.exp_phase_fast", p.exp_phase == 0x01); /* reader offers fast */

	/* fast block + cryptogram from the Kpersistent agreed in A */
	{
		uint8_t salt[ALIRO_SALT_MAX], enc[32], dec[32], fast[ALIRO_KEY_BLOCK_LEN];
		uint8_t crypt[64], payload[48], pl[160];
		size_t sl, n = 0;
		static const uint8_t zero_iv[12] = {0};

		memset(p.eph_priv, 0xE2, sizeof(p.eph_priv));
		aliro_ec_p256_pub_from_priv(p.eph_priv, p.eph_pub);

		okc("b.fast_salt",
		    aliro_salt_build(ALIRO_SALT_CRYPTOGRAM, p.txid, p.rvk + 1, p.r_eph_pub + 1,
				     p.r_id, ALIRO_IFACE_BLE, 0x0100, p.exp_phase, 0x01,
				     p.cred_pub + 1, p.a5, p.a5n, salt, &sl) == 0);
		okc("b.fast_block",
		    aliro_crypto_derive_block(p.kp, salt, sl, p.eph_pub + 1, fast) == 0);
		memset(payload, 0x11, sizeof(payload));
		okc("b.cryptogram_sealed",
		    aliro_aes256_gcm_encrypt(fast + ALIRO_CRYPTOGRAM_SK_OFFSET, zero_iv,
					     sizeof(zero_iv), NULL, 0, payload, sizeof(payload),
					     crypt, crypt + 48, 16) == 0);

		/* fast channel keys: split(fast, 0) + BleSK off the same block */
		aliro_crypto_split(fast, 0, enc, dec, p.ursk);
		memcpy(p.k_r2p, enc, 32);
		memcpy(p.k_p2r, dec, 32);
		p.r2p_ctr = p.p2r_ctr = 1;
		okc("b.ble_keys", aliro_crypto_derive_ble_keys(fast, k_ble_salt,
							       sizeof(k_ble_salt), p.ble_r2p,
							       p.ble_p2r) == 0);
		p.ble_r2p_ctr = p.ble_p2r_ctr = 1;

		n += tlv1(pl + n, ALIRO_TAG_DEVICE_PUBX, p.eph_pub, 65);
		n += tlv1(pl + n, 0x9D, crypt, 64);
		pl[n++] = 0x90;
		pl[n++] = 0x00;
		ph_send(3, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, pl, n);
	}
	okc("b.exchange_no_auth1", ph_exchange_resp(&p, 3) == 0); /* next TX is EXCHANGE */
	okc("b.ap_completed", ph_take_ap_completed(&p) == 0);
	okc("b.ranging_armed", s_rng_starts == 3);
	okc("b.ursk_match", memcmp(s_rng_ursk, p.ursk, ALIRO_URSK_LEN) == 0);
	s_cfg.cb.on_disconnected(3);
	okc("b.no_new_persist", s_nvs_stores == 3); /* fast phase mints nothing */

	printf("\n== C: failure paths ==\n");
	/* C1: untrusted credential on a provisioned (non-dev) reader */
	{
		uint8_t cred2_priv[32], cred2_pub[65];

		memset(cred2_priv, 0xC2, sizeof(cred2_priv));
		aliro_ec_p256_pub_from_priv(cred2_priv, cred2_pub);

		struct ph q;

		memset(&q, 0, sizeof(q));
		memcpy(q.rvk, p.rvk, sizeof(q.rvk));
		memcpy(q.cred_priv, cred2_priv, 32);
		memcpy(q.cred_pub, cred2_pub, 65);

		s_cfg.cb.on_connected(4);
		ph_initiate(&q, 4, 0);
		okc("c1.auth0", ph_take_auth0(&q) == 0);
		ph_auth0_resp(&q, 4, 0xE3);
		okc("c1.auth1_resp", ph_auth1_resp(&q, 4, NULL, 0) == 0);
		okc("c1.rejected_no_exchange", tx_pending() == 0);
		okc("c1.auth_cred_unchanged", aliro_reader_authenticated_credential(out65) &&
						      memcmp(out65, p.cred_pub, 65) == 0);
		s_cfg.cb.on_disconnected(4);

		/* the rejected key is the last-presented one: trust it via the
		 * bench path, exercising the store-failure rollback first */
		s_nvs_fail = true;
		okc("c1.trust_last_store_fail", aliro_reader_trust_last() == -1);
		s_nvs_fail = false;
		okc("c1.trust_last_ok", aliro_reader_trust_last() == 0);
		okc("c1.trust_last_dup", aliro_reader_trust_last() == 1);
	}

	/* C2: garbage envelope still starts the AP; a GeneralError then kills it */
	{
		static const uint8_t junk[2] = {0xDE, 0xAD};
		static const uint8_t generr[3] = {0x01, 0x01, 0x42};

		s_cfg.cb.on_connected(5);
		s_cfg.cb.on_data(5, junk, sizeof(junk)); /* unframe fails -> op-05 fallback */
		{
			struct ph q;

			memset(&q, 0, sizeof(q));
			okc("c2.auth0_after_garbage", ph_take_auth0(&q) == 0);
			ph_send(5, ALIRO_PROTO_NOTIFICATION, ALIRO_NOTIF_EVENT, generr,
				sizeof(generr));
			okc("c2.failed_after_generalerror", tx_pending() == 0);
			ph_send(5, ALIRO_PROTO_NOTIFICATION, ALIRO_NOTIF_INITIATE_AP, junk, 0);
			okc("c2.failed_ignores_msgs", tx_pending() == 0);
		}
		s_cfg.cb.on_disconnected(5);
	}

	/* C3: corrupted AUTH1Response ciphertext -> GCM open fails -> dead */
	{
		struct ph q;

		memset(&q, 0, sizeof(q));
		memcpy(q.rvk, p.rvk, sizeof(q.rvk));
		memcpy(q.cred_priv, p.cred_priv, 32);
		memcpy(q.cred_pub, p.cred_pub, 65);
		s_cfg.cb.on_connected(6);
		ph_initiate(&q, 6, 0);
		okc("c3.auth0", ph_take_auth0(&q) == 0);
		ph_auth0_resp(&q, 6, 0xE4);
		okc("c3.auth1_resp", ph_auth1_resp(&q, 6, NULL, 1 /* corrupt */) == 0);
		okc("c3.no_exchange", tx_pending() == 0);
		s_cfg.cb.on_disconnected(6);
	}

	/* C4: valid channel, signature by the wrong key -> possession fails */
	{
		struct ph q;
		uint8_t wrong[32];

		memset(&q, 0, sizeof(q));
		memcpy(q.rvk, p.rvk, sizeof(q.rvk));
		memcpy(q.cred_priv, p.cred_priv, 32);
		memcpy(q.cred_pub, p.cred_pub, 65);
		memset(wrong, 0x77, sizeof(wrong));
		s_cfg.cb.on_connected(7);
		ph_initiate(&q, 7, 0);
		okc("c4.auth0", ph_take_auth0(&q) == 0);
		ph_auth0_resp(&q, 7, 0xE5);
		okc("c4.auth1_resp", ph_auth1_resp(&q, 7, wrong, 0) == 0);
		okc("c4.no_exchange", tx_pending() == 0);
		s_cfg.cb.on_disconnected(7);
	}

	/* C7: AUTH0Response without the mandatory device ephemeral key */
	{
		struct ph q;
		static const uint8_t bad[5] = {0x99, 0x01, 0x00, 0x90, 0x00};

		memset(&q, 0, sizeof(q));
		s_cfg.cb.on_connected(8);
		ph_initiate(&q, 8, 0);
		okc("c7.auth0", ph_take_auth0(&q) == 0);
		ph_send(8, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, bad, sizeof(bad));
		okc("c7.parse_fail_no_auth1", tx_pending() == 0);
		s_cfg.cb.on_disconnected(8);
	}

	/* C5: the session table holds ALIRO_MAX_SESSIONS(2); a third connect is
	 * refused and its data dropped without a crash */
	{
		static const uint8_t ping[1] = {0x00};

		s_cfg.cb.on_connected(20);
		s_cfg.cb.on_connected(21);
		s_cfg.cb.on_connected(22); /* no free slot */
		ph_send(22, ALIRO_PROTO_NOTIFICATION, ALIRO_NOTIF_INITIATE_AP, ping,
			sizeof(ping));
		okc("c5.overflow_conn_dropped", tx_pending() == 0);
		s_cfg.cb.on_disconnected(22);
		s_cfg.cb.on_disconnected(21);
		s_cfg.cb.on_disconnected(20);
	}

	printf("\n== D: trust clear, adv refresh, attach mode, provision clear ==\n");
	okc("d.trust_clear", aliro_reader_trust_clear() == 0);
	okc("d.trust_clear_again", aliro_reader_trust_clear() == 1);

	/* cleared trust store also dropped the Kpersistent: fast no longer offered */
	{
		struct ph q;

		memset(&q, 0, sizeof(q));
		s_cfg.cb.on_connected(9);
		ph_initiate(&q, 9, 0);
		okc("d.auth0_back_to_std", ph_take_auth0(&q) == 0 && q.exp_phase == 0x00);
		s_cfg.cb.on_disconnected(9); /* abandon mid-AUTH0: teardown must cope */
	}

	/* adv refresh: no-op on a zero GRK, live once one is provisioned */
	aliro_reader_refresh_adv();
	okc("d.refresh_noop_zero_grk", s_readv == 0);
	{
		uint8_t grk[16];

		memset(grk, 0xAB, sizeof(grk));
		okc("d.provision_grk", aliro_reader_provision_identity(rid, sp, grk) == 0);
		aliro_reader_refresh_adv();
		okc("d.refresh_readvertises",
		    s_readv == 1 && s_adv_sets >= 1 && s_adv_grk[0] == 0xAB);
	}

	/* attach-mode entry points */
	okc("d.ble_prepare", aliro_reader_ble_prepare() != NULL);
	{
		int sets = s_adv_sets;

		okc("d.start_attached", aliro_reader_start_attached() == 0);
		okc("d.attached_adv_params", s_adv_sets == sets + 1); /* GRK present */
	}

	/* provisioning error paths + reset to the dev identity */
	s_nvs_fail = true;
	okc("d.provision_id_store_fail", aliro_reader_provision_identity(rid, sp, grk0) == -1);
	okc("d.provision_clear_store_fail", aliro_reader_provision_clear() == -1);
	s_nvs_fail = false;
	{
		uint8_t badpt[65];

		memset(badpt, 0x05, sizeof(badpt)); /* not an uncompressed point */
		okc("d.provision_bad_point", aliro_reader_provision_add_trust(badpt) == -1);
	}
	okc("d.provision_clear", aliro_reader_provision_clear() == 0);
	{
		struct aliro_reader_identity id2;
		struct aliro_trust_store ts2;

		okc("d.dev_identity_persisted",
		    aliro_prov_load(&id2, &ts2) == 0 && id2.is_dev && ts2.count == 0);
	}

	printf("\n== E: failure injection — bring-up, provisioning, walk-up error paths ==\n");
	tx_reset(); /* every earlier frame is consumed; reclaim the queue */

	/* E1: engine/transport bring-up failures */
	aliro_prim_host_fail_init = 1;
	okc("e1.start_crypto_fail", aliro_reader_start() == -1);
	aliro_prim_host_fail_init = 1;
	okc("e1.start_attached_crypto_fail", aliro_reader_start_attached() == -1);
	s_ble_prepare_fail = true;
	okc("e1.ble_prepare_fail", aliro_reader_ble_prepare() == NULL);
	s_ble_prepare_fail = false;
	s_rng_init_fail = 1; /* ranging adapter down: start still succeeds, auth-only */
	okc("e1.start_ranging_unavailable", aliro_reader_start() == 0);

	/* E2: provisioning + trust console error paths (dev identity, empty store) */
	uint8_t idle_priv[32], idle_pub[65];

	memset(idle_priv, 0xB7, sizeof(idle_priv));
	aliro_ec_p256_pub_from_priv(idle_priv, idle_pub);
	okc("e2.add_trust", aliro_reader_provision_add_trust(idle_pub) == 0);
	okc("e2.add_trust_dup", aliro_reader_provision_add_trust(idle_pub) == 1);
	s_nvs_fail = true;
	okc("e2.add_trust_store_fail", aliro_reader_provision_add_trust(p.cred_pub) == -1);
	okc("e2.trust_clear_store_fail", aliro_reader_trust_clear() == -1);
	s_nvs_fail = false;
	aliro_reader_prov_print(); /* anchor-listing loop + last-cred hexdump */
	okc("e2.trust_clear", aliro_reader_trust_clear() == 0);

	/* E3: provisioned identity + two anchors (an idle one first, so the fast
	 * trial later has a no-Kpersistent slot to skip over) */
	okc("e3.provision_id", aliro_reader_provision_identity(rid, sp, grk0) == 0);
	okc("e3.trust_idle", aliro_reader_provision_add_trust(idle_pub) == 0);
	okc("e3.trust_walker", aliro_reader_provision_add_trust(p.cred_pub) == 0);
	aliro_ec_p256_pub_from_priv(sp, p.rvk);

	/* E4: AUTH0-response failure paths */
	{
		size_t dn;

		/* too short for the SW */
		s_cfg.cb.on_connected(30);
		ph_initiate(&p, 30, 0);
		okc("e4.auth0_a", ph_take_auth0(&p) == 0);
		{
			static const uint8_t one[1] = {0x00};

			ph_send(30, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, one, sizeof(one));
		}
		okc("e4.short_auth0resp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(30);

		/* device ephemeral that is not an uncompressed point: ECDH fails */
		s_cfg.cb.on_connected(31);
		ph_initiate(&p, 31, 0);
		okc("e4.auth0_b", ph_take_auth0(&p) == 0);
		{
			uint8_t badpub[65], bpl[80];
			size_t bn;

			memset(badpub, 0x05, sizeof(badpub)); /* prefix != 0x04 */
			bn = tlv1(bpl, ALIRO_TAG_DEVICE_PUBX, badpub, 65);
			bpl[bn++] = 0x90;
			bpl[bn++] = 0x00;
			ph_send(31, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, bpl, bn);
		}
		okc("e4.ecdh_fail_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(31);

		/* live ephemeral generation failures: a concurrent session consumes
		 * the spare pair, so start_auth must generate inline — and fail */
		s_cfg.cb.on_connected(32);
		ph_initiate(&p, 32, 0); /* consumes the spare */
		okc("e4.auth0_c", tx_next(&dn) != NULL);
		s_cfg.cb.on_connected(33);
		aliro_prim_host_fail_keygen = 1;
		ph_initiate(&p, 33, 0);
		okc("e4.keygen_fail_no_auth0", tx_pending() == 0);
		s_cfg.cb.on_disconnected(33); /* refills the spare */
		s_cfg.cb.on_disconnected(32);
		s_cfg.cb.on_connected(34);
		ph_initiate(&p, 34, 0); /* consumes the refilled spare */
		okc("e4.auth0_d", tx_next(&dn) != NULL);
		s_cfg.cb.on_connected(35);
		aliro_prim_host_fail_random_after = 1; /* keygen draws once, txid fails */
		ph_initiate(&p, 35, 0);
		okc("e4.random_fail_no_auth0", tx_pending() == 0);
		s_cfg.cb.on_disconnected(35);
		s_cfg.cb.on_disconnected(34);

		/* reader-transcript signature failure */
		s_cfg.cb.on_connected(36);
		ph_initiate(&p, 36, 0);
		okc("e4.auth0_e", ph_take_auth0(&p) == 0);
		aliro_prim_host_fail_sign = 1;
		ph_auth0_resp(&p, 36, 0xE6);
		okc("e4.sign_fail_no_auth1", tx_pending() == 0);
		s_cfg.cb.on_disconnected(36);
	}

	/* E5: AUTH1-response failure paths (each walk-up reaches SENT_AUTH1) */
	{
		size_t dn;

		/* too short for the SW */
		s_cfg.cb.on_connected(37);
		ph_initiate(&p, 37, 0);
		okc("e5.auth0_a", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 37, 0xE7);
		okc("e5.auth1_sent_a", tx_next(&dn) != NULL);
		{
			static const uint8_t one[1] = {0x22};

			ph_send(37, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, one, sizeof(one));
		}
		okc("e5.short_auth1resp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(37);

		/* shorter than a GCM tag once the SW is stripped */
		s_cfg.cb.on_connected(38);
		ph_initiate(&p, 38, 0);
		okc("e5.auth0_b", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 38, 0xE8);
		okc("e5.auth1_sent_b", tx_next(&dn) != NULL);
		{
			uint8_t small[10] = {0};

			small[8] = 0x90;
			small[9] = 0x00;
			ph_send(38, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, small,
				sizeof(small));
		}
		okc("e5.tagless_auth1resp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(38);

		/* ciphertext larger than the decrypt scratch buffer */
		s_cfg.cb.on_connected(39);
		ph_initiate(&p, 39, 0);
		okc("e5.auth0_c", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 39, 0xE9);
		okc("e5.auth1_sent_c", tx_next(&dn) != NULL);
		{
			uint8_t big[300];

			memset(big, 0x33, sizeof(big));
			big[298] = 0x90;
			big[299] = 0x00;
			ph_send(39, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, big, sizeof(big));
		}
		okc("e5.oversize_auth1resp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(39);

		/* decrypts fine, parses as nothing */
		s_cfg.cb.on_connected(40);
		ph_initiate(&p, 40, 0);
		okc("e5.auth0_d", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 40, 0xEA);
		okc("e5.junk_pt_sealed", ph_auth1_resp_v(&p, 40, 1, 0x90, 0x00) == 0);
		okc("e5.junk_pt_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(40);
	}

	/* E6: full standard walk-up under odd SWs + established-phase failures */
	{
		int stores;

		s_cfg.cb.on_connected(41);
		ph_initiate(&p, 41, 1);
		okc("e6.auth0", ph_take_auth0(&p) == 0);
		ph_auth0_resp_sw2(&p, 41, 0xEB, 0x6A, 0x80); /* odd SW: warn + continue */
		okc("e6.auth1_resp_sw", ph_auth1_resp_v(&p, 41, 0, 0x69, 0x82) == 0);
		s_rng_start_fail = 1; /* ranging adapter refuses to arm: warn only */
		okc("e6.exchange", ph_exchange_resp(&p, 41) == 0);
		okc("e6.ap_completed", ph_take_ap_completed(&p) == 0);

		/* an SDU the BleSK channel cannot open is dumped, not fed */
		{
			static const uint8_t junk[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x55};
			int feeds = s_rng_feeds;

			s_cfg.cb.on_data(41, junk, sizeof(junk));
			okc("e6.bad_sdu_not_fed", s_rng_feeds == feeds);
		}

		/* reader-status seal failure: nothing goes on the wire */
		aliro_prim_host_fail_encrypt_after = 0;
		aliro_reader_notify_unlock(true);
		okc("e6.notify_seal_fail", tx_pending() == 0);

		/* the standard phase minted a Kpersistent; the persist fails on this
		 * disconnect and is retried on a later one */
		stores = s_nvs_stores;
		s_nvs_fail = true;
		s_cfg.cb.on_disconnected(41);
		okc("e6.kp_persist_deferred", s_nvs_stores == stores);
		s_nvs_fail = false;
	}

	/* E7: expedited-fast — skipped anchor, no-match fallback, lost group key */
	tx_reset();
	{
		/* fast succeeds; the anchor without a Kpersistent is skipped over */
		s_cfg.cb.on_connected(42);
		ph_initiate(&p, 42, 1);
		okc("e7.auth0_fast_offered", ph_take_auth0(&p) == 0 && p.exp_phase == 0x01);
		okc("e7.fast_resp", ph_auth0_resp_fast(&p, 42, 0xEC) == 0);
		okc("e7.exchange", ph_exchange_resp(&p, 42) == 0);
		okc("e7.ap_completed", ph_take_ap_completed(&p) == 0);
		s_cfg.cb.on_disconnected(42); /* also retries the deferred persist */

		/* garbage cryptogram: no Kpersistent matches, standard continues */
		s_cfg.cb.on_connected(43);
		ph_initiate(&p, 43, 1);
		okc("e7.auth0_b", ph_take_auth0(&p) == 0 && p.exp_phase == 0x01);
		ph_auth0_resp_badcrypt(&p, 43, 0xED);
		okc("e7.nomatch_auth1_resp", ph_auth1_resp(&p, 43, NULL, 0) == 0);
		okc("e7.nomatch_exchange", ph_exchange_resp(&p, 43) == 0);
		okc("e7.nomatch_ap_completed", ph_take_ap_completed(&p) == 0);
		s_cfg.cb.on_disconnected(43);

		/* group key lost: provisioning commits but the fast trial and the
		 * standard AUTH1 both dead-end on the missing salt field 1 */
		aliro_prim_host_fail_pub_from_priv = 1;
		okc("e7.provision_no_group_x",
		    aliro_reader_provision_identity(rid, sp, grk0) == 0);
		s_cfg.cb.on_connected(44);
		ph_initiate(&p, 44, 1);
		okc("e7.auth0_c", ph_take_auth0(&p) == 0 && p.exp_phase == 0x01);
		okc("e7.fast_resp_c", ph_auth0_resp_fast(&p, 44, 0xEE) == 0);
		okc("e7.auth1_resp_c", ph_auth1_resp(&p, 44, NULL, 0) == 0);
		okc("e7.no_group_x_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(44);
		okc("e7.provision_restore", aliro_reader_provision_identity(rid, sp, grk0) == 0);
	}

	/* E8: EXCHANGE-response failures + seal failures on the reader side */
	tx_reset();
	{
		size_t dn;

		/* response too short for the SW */
		s_cfg.cb.on_connected(46);
		ph_initiate(&p, 46, 0);
		okc("e8.auth0_a", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 46, 0xF1);
		okc("e8.auth1_resp_a", ph_auth1_resp(&p, 46, NULL, 0) == 0);
		okc("e8.exchange_sent_a", tx_next(&dn) != NULL);
		{
			static const uint8_t one[1] = {0x11};

			ph_send(46, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, one, sizeof(one));
		}
		okc("e8.short_exresp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(46);

		/* response body that cannot decrypt */
		s_cfg.cb.on_connected(47);
		ph_initiate(&p, 47, 0);
		okc("e8.auth0_b", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 47, 0xF2);
		okc("e8.auth1_resp_b", ph_auth1_resp(&p, 47, NULL, 0) == 0);
		okc("e8.exchange_sent_b", tx_next(&dn) != NULL);
		{
			uint8_t junk[40];

			memset(junk, 0x5C, sizeof(junk));
			junk[38] = 0x90;
			junk[39] = 0x00;
			ph_send(47, ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, junk, sizeof(junk));
		}
		okc("e8.garbage_exresp_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(47);

		/* device rejects the EXCHANGE with an error status */
		s_cfg.cb.on_connected(48);
		ph_initiate(&p, 48, 0);
		okc("e8.auth0_c", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 48, 0xF3);
		okc("e8.auth1_resp_c", ph_auth1_resp(&p, 48, NULL, 0) == 0);
		okc("e8.exchange_err", ph_exchange_err(&p, 48) == 0);
		okc("e8.exchange_err_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(48);

		/* the reader's own EXCHANGE seal fails (after the phone's seal) */
		s_cfg.cb.on_connected(49);
		ph_initiate(&p, 49, 0);
		okc("e8.auth0_d", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 49, 0xF4);
		aliro_prim_host_fail_encrypt_after = 1; /* phone seals once, reader fails */
		okc("e8.auth1_resp_d", ph_auth1_resp(&p, 49, NULL, 0) == 0);
		okc("e8.exchange_seal_fail_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(49);

		/* the reader's AP-Completed seal fails (after the phone's seal) */
		s_cfg.cb.on_connected(50);
		ph_initiate(&p, 50, 0);
		okc("e8.auth0_e", ph_take_auth0(&p) == 0);
		ph_auth0_resp(&p, 50, 0xF5);
		okc("e8.auth1_resp_e", ph_auth1_resp(&p, 50, NULL, 0) == 0);
		aliro_prim_host_fail_encrypt_after = 1; /* phone ok-body, then AP seal */
		okc("e8.exchange_resp_e", ph_exchange_resp(&p, 50) == 0);
		okc("e8.ap_seal_fail_dead", tx_pending() == 0);
		s_cfg.cb.on_disconnected(50);
	}

	/* E9: trust store filled to the brim + a rejected walk-up: trust_last
	 * has a presented key but nowhere to put it */
	tx_reset();
	{
		struct ph q;
		uint8_t fill_priv[32], fill_pub[65];

		memset(fill_priv, 0xB8, sizeof(fill_priv));
		aliro_ec_p256_pub_from_priv(fill_priv, fill_pub);
		okc("e9.fill3", aliro_reader_provision_add_trust(fill_pub) == 0);
		memset(fill_priv, 0xB9, sizeof(fill_priv));
		aliro_ec_p256_pub_from_priv(fill_priv, fill_pub);
		okc("e9.fill4", aliro_reader_provision_add_trust(fill_pub) == 0);

		memset(&q, 0, sizeof(q));
		memcpy(q.rvk, p.rvk, sizeof(q.rvk));
		memset(q.cred_priv, 0xC9, sizeof(q.cred_priv));
		aliro_ec_p256_pub_from_priv(q.cred_priv, q.cred_pub);
		s_cfg.cb.on_connected(45);
		ph_initiate(&q, 45, 0);
		okc("e9.auth0", ph_take_auth0(&q) == 0);
		ph_auth0_resp(&q, 45, 0xEF);
		okc("e9.auth1_resp", ph_auth1_resp(&q, 45, NULL, 0) == 0);
		okc("e9.rejected", tx_pending() == 0);
		s_cfg.cb.on_disconnected(45);
		okc("e9.trust_last_full", aliro_reader_trust_last() == -1);
	}

	/* console/status entry points: exercised for effect-free execution */
	aliro_reader_prov_print();
	aliro_reader_stepup_arm();
	aliro_reader_stepup_status();

	printf("\n%s: %d failure(s)\n", fails == 0 ? "PASS" : "FAIL", fails);
	return fails != 0;
}
