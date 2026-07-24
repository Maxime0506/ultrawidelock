/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host tests for the Aliro initiator (device) side: aliro_device_apdu (inverse
 * wire codec) + aliro_device (AP secure channel, cryptogram sealer, standard-path
 * key schedule). Everything here is EC-free and CI-gating; the full ECDH+ECDSA
 * handshake loopback is compiled behind ALIRO_DEVICE_HAVE_EC (real EC only exists
 * on-target, aliro_prim_psa.c) and is exercised on the bench, mirroring how the
 * repo already gates verify_port.sh.
 *
 * Anti-self-consistency: the device builders are pinned to the SHIPPED reader
 * parsers (round-trip), to the reader's own AES-GCM channel (interop), and — for
 * the cryptogram sealer — byte-exact to the Aliro §14.4 worked example. A green
 * device-vs-device loopback proves consistency; these external anchors + the
 * on-iPhone bench test prove correctness.
 */
#include <stdio.h>
#include <string.h>

#include "aliro_apdu.h"
#include "aliro_crypto.h"
#include "aliro_device.h"
#include "aliro_device_apdu.h"
#include "aliro_prim.h"

static int fails;

static void hx(char *d, const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		sprintf(d + 2 * i, "%02x", b[i]);
	}
}

static int uh(uint8_t *d, const char *h)
{
	size_t n = strlen(h) / 2;

	for (size_t i = 0; i < n; i++) {
		unsigned v;

		sscanf(h + 2 * i, "%2x", &v);
		d[i] = (uint8_t)v;
	}
	return (int)n;
}

static void chk(const char *name, const uint8_t *got, size_t n, const char *want)
{
	char g[512];

	hx(g, got, n);
	if (strcmp(g, want) != 0) {
		printf("  FAIL %-28s\n    got  %s\n    want %s\n", name, g, want);
		fails++;
	} else {
		printf("  ok   %-28s %s\n", name, g);
	}
}

static void t_ok_(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

#define T_OK(name, cond) t_ok_((name), (cond))

/* Same CSA v1.0 default 0xA5 salt TLV the reader and device both use. */
static const uint8_t k_a5[] = {0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00};

/* EC comes from aliro_prim_host.c's fake-curve double (added by the reader-tests
 * suite): a self-consistent P-256 stand-in (symmetric ECDH, round-tripping
 * ECDSA). With it present the full standard-path loopback below runs host-side
 * under -DALIRO_DEVICE_HAVE_EC; the byte-exact anchors above still pin spec truth. */

/* ---- test 1: inverse wire codec round-trips against the shipped reader ---- */

static void test_codec(void)
{
	printf("\n== device inverse codec (round-trip vs shipped reader) ==\n");

	uint8_t reph[65], txid[16], rid[32];

	for (int i = 0; i < 65; i++) {
		reph[i] = (uint8_t)(i == 0 ? 0x04 : 0x10 + i);
	}
	for (int i = 0; i < 16; i++) {
		txid[i] = (uint8_t)(0xA0 + i);
	}
	for (int i = 0; i < 32; i++) {
		rid[i] = (uint8_t)(0x30 + i);
	}

	/* AUTH0: reader build+wrap -> device unwrap+parse */
	uint8_t tlv[200], apdu[220];
	size_t tn, an;

	T_OK("auth0.build", aliro_apdu_build_auth0(0x00u, 0x01u, 0x0100u, reph, txid, rid, tlv,
						   sizeof(tlv), &tn) == 0);
	T_OK("auth0.wrap", aliro_apdu_wrap(ALIRO_INS_AUTH0, tlv, tn, apdu, sizeof(apdu), &an) == 0);

	uint8_t ins;
	const uint8_t *data;
	size_t dlen;
	struct aliro_auth0_command a0c;

	T_OK("auth0.unwrap",
	     aliro_apdu_unwrap(apdu, an, &ins, &data, &dlen) == 0 && ins == ALIRO_INS_AUTH0);
	T_OK("auth0.parse", aliro_dev_parse_auth0_cmd(data, dlen, &a0c) == 0);
	T_OK("auth0.fields",
	     a0c.exp_phase == 0x00u && a0c.user_policy == 0x01u && a0c.version == 0x0100u &&
		     memcmp(a0c.reader_eph_pub, reph, 65) == 0 && memcmp(a0c.txid, txid, 16) == 0 &&
		     memcmp(a0c.reader_id, rid, 32) == 0);

	/* AUTH0Response: device build+SW -> reader strip+parse */
	uint8_t deph[65], resp[256];
	size_t rn;

	for (int i = 0; i < 65; i++) {
		deph[i] = (uint8_t)(i == 0 ? 0x04 : 0x80 + i);
	}
	T_OK("auth0resp.build",
	     aliro_dev_build_auth0_resp(deph, NULL, resp, sizeof(resp), &rn) == 0 &&
		     aliro_apdu_append_sw(resp, &rn, sizeof(resp), 0x9000u) == 0);

	size_t rl = rn;
	uint16_t sw;
	struct aliro_auth0_response a0r;

	T_OK("auth0resp.strip", aliro_apdu_strip_sw(resp, &rl, &sw) == 0 && sw == 0x9000u);
	T_OK("auth0resp.parse", aliro_apdu_parse_auth0_response(resp, rl, &a0r) == 0 &&
					memcmp(a0r.device_eph_pub, deph, 65) == 0 &&
					!a0r.have_cryptogram);

	/* AUTH1: reader build+wrap -> device unwrap+parse */
	uint8_t rsig[64];

	for (int i = 0; i < 64; i++) {
		rsig[i] = (uint8_t)(0xC0 + i);
	}
	T_OK("auth1.build", aliro_apdu_build_auth1(0x01u, rsig, tlv, sizeof(tlv), &tn) == 0);
	T_OK("auth1.wrap", aliro_apdu_wrap(ALIRO_INS_AUTH1, tlv, tn, apdu, sizeof(apdu), &an) == 0);

	struct aliro_auth1_command a1c;

	T_OK("auth1.unwrap+parse",
	     aliro_apdu_unwrap(apdu, an, &ins, &data, &dlen) == 0 && ins == ALIRO_INS_AUTH1 &&
		     aliro_dev_parse_auth1_cmd(data, dlen, &a1c) == 0 && a1c.cred_type == 0x01u &&
		     memcmp(a1c.reader_sig, rsig, 64) == 0);

	/* AUTH1Response plaintext: device build -> reader parse */
	uint8_t dsig[64], dpub[65];

	for (int i = 0; i < 64; i++) {
		dsig[i] = (uint8_t)(0x40 + i);
	}
	for (int i = 0; i < 65; i++) {
		dpub[i] = (uint8_t)(i == 0 ? 0x04 : 0x20 + i);
	}

	uint8_t plain[200];
	size_t pn;
	struct aliro_auth1_response a1r;

	T_OK("auth1resp.build",
	     aliro_dev_build_auth1_resp(dsig, dpub, plain, sizeof(plain), &pn) == 0);
	T_OK("auth1resp.parse", aliro_apdu_parse_auth1_response(plain, pn, &a1r) == 0 &&
					memcmp(a1r.device_sig, dsig, 64) == 0 &&
					a1r.have_device_pub &&
					memcmp(a1r.device_pub, dpub, 65) == 0);

	/* EXCHANGE command plaintext: reader build -> device parse */
	uint8_t ex[16];
	size_t exn;
	struct aliro_exchange_command exc;

	T_OK("exchange.build", aliro_apdu_build_exchange(0, 0, 1, ex, sizeof(ex), &exn) == 0);
	T_OK("exchange.parse", aliro_dev_parse_exchange_cmd(ex, exn, &exc) == 0 && exc.ursk_ready &&
				       !exc.have_status);

	/* unwrap rejects a malformed/short APDU */
	uint8_t bad[3] = {0x80, 0x80, 0x00};

	T_OK("unwrap.rejects-short", aliro_apdu_unwrap(bad, sizeof(bad), &ins, &data, &dlen) != 0);
}

/* ---- test 2: fast-phase cryptogram sealer, byte-exact to Aliro §14.4 ---- */

static void test_cryptogram(void)
{
	printf("\n== device cryptogram sealer (§14.4 byte-exact) ==\n");

	uint8_t csk[32], pt[48], out[64], pt2[64];

	uh(csk, "46b35933b497ead9d72e024b267ce1db9a59ba54fc73d46bda3149a8b047bcaf");
	uh(pt, "5e02003f911400000000000000000000000000000000000000009214000000"
	       "0000000000000000000000000000000000");

	T_OK("cryptogram.seal.ok", aliro_dev_seal_cryptogram(csk, pt, 48, out) == 0);
	/* the device's sealed cryptogram must equal the spec's worked-example bytes */
	chk("cryptogram.seal==§14.4", out, 64,
	    "ba76234a1e427f9e463106251fb9e9edc5f5812f59fd887d4e57eb0bc544b7cb"
	    "9d368c4dedadf782d520a91f9666b9091e0973894522c04b142f6447b596942a");
	/* and the reader's own verifier must accept it and recover the plaintext */
	T_OK("cryptogram.verify-roundtrip",
	     aliro_crypto_verify_cryptogram(csk, out, 64, pt2) == 0 && memcmp(pt2, pt, 48) == 0);
}

/* ---- test 3: device AP channel interops with the shipped reader channel ---- */

static void test_secchan(void)
{
	printf("\n== device AP channel <-> reader aliro_secchan interop ==\n");

	uint8_t s0[32], s1[32];

	for (int i = 0; i < 32; i++) {
		s0[i] = (uint8_t)(0x11 + i);
		s1[i] = (uint8_t)(0x91 + i);
	}
	struct aliro_secchan scr;     /* reader */
	struct aliro_dev_secchan scd; /* device */

	aliro_secchan_init(&scr, s0, s1);
	aliro_dev_secchan_init(&scd, s0, s1);

	/* reader -> device, twice (counters must advance in lockstep) */
	for (int round = 0; round < 2; round++) {
		uint8_t msg[24], ct[24], tag[16], got[24];

		memset(msg, 0x30 + round, sizeof(msg));
		T_OK("r2d.seal", aliro_secchan_seal(&scr, NULL, 0, msg, sizeof(msg), ct, tag) == 0);
		T_OK("r2d.device-opens",
		     aliro_dev_secchan_open(&scd, ct, sizeof(msg), tag, got) == 0 &&
			     memcmp(got, msg, sizeof(msg)) == 0);
	}

	/* device -> reader, twice */
	for (int round = 0; round < 2; round++) {
		uint8_t msg[20], ct[20], tag[16], got[20];

		memset(msg, 0x60 + round, sizeof(msg));
		T_OK("d2r.seal", aliro_dev_secchan_seal(&scd, msg, sizeof(msg), ct, tag) == 0);
		T_OK("d2r.reader-opens",
		     aliro_secchan_open(&scr, NULL, 0, ct, sizeof(msg), tag, got) == 0 &&
			     memcmp(got, msg, sizeof(msg)) == 0);
	}
}

/* ---- test 4: standard-path key schedule equals the reader's (EC-free) ---- */

static void test_key_schedule(void)
{
	printf("\n== device session key schedule == reader (EC-free) ==\n");

	uint8_t shared[32], txid[16], rgx[32], rex[32], rid[32], dex[32];

	for (int i = 0; i < 32; i++) {
		shared[i] = (uint8_t)(0x40 + i);
		rgx[i] = (uint8_t)(0x01 + i);
		rex[i] = (uint8_t)(0x81 + i);
		rid[i] = (uint8_t)(0x11 * (i & 0x0f));
		dex[i] = (uint8_t)(0x80 + i);
	}
	for (int i = 0; i < 16; i++) {
		txid[i] = (uint8_t)(0xF0 - i);
	}

	/* reference (reader) path via aliro_crypto directly */
	uint8_t z[32], salt[ALIRO_SALT_MAX], block[ALIRO_KEY_BLOCK_LEN];
	uint8_t enc[32], dec[32], ursk_exp[32];
	size_t slen;

	aliro_crypto_derive_z(shared, txid, z);
	T_OK("ref.salt",
	     aliro_salt_build(ALIRO_SALT_SESSION, txid, rgx, rex, rid, ALIRO_IFACE_BLE, 0x0100u,
			      0x00u, 0x01u, NULL, k_a5, sizeof(k_a5), salt, &slen) == 0);
	T_OK("ref.block", aliro_crypto_derive_block(z, salt, slen, dex, block) == 0);
	aliro_crypto_split(block, 1, enc, dec, ursk_exp);

	/* device path */
	struct aliro_dev_secchan sc;
	uint8_t ursk_dev[32];

	T_OK("dev.derive", aliro_device_derive_session(shared, txid, rgx, rex, rid, 0x00u, k_a5,
						       sizeof(k_a5), dex, &sc, ursk_dev) == 0);
	T_OK("dev.ursk==reader", memcmp(ursk_dev, ursk_exp, 32) == 0);
	T_OK("dev.s0==S0(reader->device)", memcmp(sc.s0, enc, 32) == 0);
	T_OK("dev.s1==S1(device->reader)", memcmp(sc.s1, dec, 32) == 0);
}

/* ---- test 4b: device BleSK ranging channel <-> the reader's aliro_msg_* ---- */

static void test_blesk_channel(void)
{
	printf("\n== device BleSK ranging channel <-> reader aliro_msg_seal/open ==\n");

	/* A derived 160-byte key block; only block[96..127] (BleSK) is consumed here.
	 * Both roles derive their directional keys from the SAME block + versions salt. */
	uint8_t block[ALIRO_KEY_BLOCK_LEN];

	for (int i = 0; i < (int)sizeof(block); i++) {
		block[i] = (uint8_t)(0x37u * (unsigned)i + 0x11u);
	}
	/* v1.0-only ranging-channel salt = reader_versions || selected_version, i.e.
	 * 01 00 01 00 (matches the reader's init_ble_channel + test_aliro_reader.c). */
	static const uint8_t ble_salt[] = {0x01, 0x00, 0x01, 0x00};

	/* reader side: the shipped primitives */
	uint8_t br[32], bd[32];
	struct aliro_secchan r_ble;

	T_OK("blesk.derive",
	     aliro_crypto_derive_ble_keys(block, ble_salt, sizeof(ble_salt), br, bd) == 0);
	aliro_secchan_init(&r_ble, br, bd);

	/* device side: the new mirror derives the same two directional keys */
	struct aliro_dev_secchan d_ble;

	T_OK("blesk.dev-init", aliro_dev_blesk_init(&d_ble, block, ble_salt, sizeof(ble_salt)) == 0);
	T_OK("blesk.s0==BleSKReader", memcmp(d_ble.s0, br, 32) == 0);
	T_OK("blesk.s1==BleSKDevice", memcmp(d_ble.s1, bd, 32) == 0);

	/* reader->device: reader seals (dir 0), device opens (dir 0). Two SDUs prove
	 * counter continuity. First = the real Reader-Status AP-Completed plaintext. */
	static const uint8_t ap_completed[] = {0x02, 0x03, 0x00, 0x04, 0x00, 0x02, 0x20, 0x00};
	static const uint8_t m1_ish[] = {0x01, 0x00, 0x00, 0x04, 0xde, 0xad, 0xbe, 0xef};
	const uint8_t *const r2d[2] = {ap_completed, m1_ish};

	for (int i = 0; i < 2; i++) {
		uint8_t wire[64], got[64];
		size_t wl = 0, gl = 0;

		T_OK("blesk.r2d.reader-seals",
		     aliro_msg_seal(&r_ble, r2d[i], 8, wire, sizeof(wire), &wl) == 0);
		T_OK("blesk.r2d.device-opens",
		     aliro_dev_ble_open(&d_ble, wire, wl, got, sizeof(got), &gl) == 0 && gl == 8 &&
			     memcmp(got, r2d[i], 8) == 0);
	}

	/* device->reader: device seals (dir 1), reader opens (dir 1). Mixed lengths. */
	static const uint8_t d2r_a[] = {0x02, 0x02, 0x00, 0x04, 0x00, 0x02, 0x04, 0x01};
	static const uint8_t d2r_b[] = {0x01, 0x01, 0x00, 0x02, 0xca, 0xfe};
	const uint8_t *const d2r[2] = {d2r_a, d2r_b};
	const size_t d2r_len[2] = {sizeof(d2r_a), sizeof(d2r_b)};

	for (int i = 0; i < 2; i++) {
		uint8_t wire[64], got[64];
		size_t wl = 0, gl = 0;

		T_OK("blesk.d2r.device-seals",
		     aliro_dev_ble_seal(&d_ble, d2r[i], d2r_len[i], wire, sizeof(wire), &wl) == 0);
		T_OK("blesk.d2r.reader-opens",
		     aliro_msg_open(&r_ble, wire, wl, got, sizeof(got), &gl) == 0 &&
			     gl == d2r_len[i] && memcmp(got, d2r[i], d2r_len[i]) == 0);
	}

	/* a flipped ciphertext byte must fail the device open (auth, not just parse). */
	{
		struct aliro_dev_secchan fresh;
		struct aliro_secchan rfresh;
		uint8_t wire[64], got[64];
		size_t wl = 0, gl = 0;

		aliro_secchan_init(&rfresh, br, bd);
		aliro_dev_blesk_init(&fresh, block, ble_salt, sizeof(ble_salt));
		T_OK("blesk.tamper.seal",
		     aliro_msg_seal(&rfresh, ap_completed, 8, wire, sizeof(wire), &wl) == 0);
		wire[5] ^= 0x01u;
		T_OK("blesk.tamper.rejected",
		     aliro_dev_ble_open(&fresh, wire, wl, got, sizeof(got), &gl) != 0);
	}
}

/* ---- test 5: full standard-path loopback (target-gated: needs real EC) ---- */

#if defined(ALIRO_DEVICE_HAVE_EC)
static int loopback(void)
{
	uint8_t reader_sign_priv[32], reader_verif_pub[65], reader_group_x[32], reader_id[32];
	uint8_t cred_priv[32];

	if (aliro_random(reader_sign_priv, 32) != 0 || aliro_random(cred_priv, 32) != 0) {
		return -1;
	}
	for (int i = 0; i < 32; i++) {
		reader_id[i] = (uint8_t)(0x30 + i);
	}
	if (aliro_ec_p256_pub_from_priv(reader_sign_priv, reader_verif_pub) != 0) {
		return -1;
	}
	memcpy(reader_group_x, reader_verif_pub + 1, 32);

	struct aliro_device dev;

	if (aliro_device_init(&dev, cred_priv, reader_id, reader_verif_pub) != 0) {
		return -1;
	}

	/* reader: ephemeral + txid -> AUTH0 */
	uint8_t reph_priv[32], reph_pub[65], txid[16];

	if (aliro_ec_p256_keygen(reph_priv, reph_pub) != 0 || aliro_random(txid, 16) != 0) {
		return -1;
	}

	uint8_t tlv[200], apdu[220], resp[256];
	size_t tn, an, rn, rl;
	uint16_t sw;

	if (aliro_apdu_build_auth0(0x00u, 0x01u, 0x0100u, reph_pub, txid, reader_id, tlv,
				   sizeof(tlv), &tn) != 0 ||
	    aliro_apdu_wrap(ALIRO_INS_AUTH0, tlv, tn, apdu, sizeof(apdu), &an) != 0 ||
	    aliro_device_on_command(&dev, apdu, an, resp, sizeof(resp), &rn) != 0) {
		return -1;
	}

	/* reader parses AUTH0Response */
	struct aliro_auth0_response a0;
	uint8_t device_eph_pub[65];

	rl = rn;
	if (aliro_apdu_strip_sw(resp, &rl, &sw) != 0 ||
	    aliro_apdu_parse_auth0_response(resp, rl, &a0) != 0) {
		return -1;
	}
	memcpy(device_eph_pub, a0.device_eph_pub, 65);

	/* reader: ECDH, z, sign reader transcript -> AUTH1 */
	uint8_t shared[32], z[32], td[160], sig[64];
	size_t tdn;

	if (aliro_ecdh_p256(reph_priv, device_eph_pub, shared) != 0) {
		return -1;
	}
	aliro_crypto_derive_z(shared, txid, z);
	if (aliro_apdu_build_authdata(ALIRO_AUTH_READER, reader_id, device_eph_pub + 1,
				      reph_pub + 1, txid, td, sizeof(td), &tdn) != 0 ||
	    aliro_ecdsa_p256_sign(reader_sign_priv, td, tdn, sig) != 0 ||
	    aliro_apdu_build_auth1(0x01u, sig, tlv, sizeof(tlv), &tn) != 0 ||
	    aliro_apdu_wrap(ALIRO_INS_AUTH1, tlv, tn, apdu, sizeof(apdu), &an) != 0 ||
	    aliro_device_on_command(&dev, apdu, an, resp, sizeof(resp), &rn) != 0) {
		return -1;
	}

	/* reader: derive session, open + parse AUTH1Response, verify device signature */
	uint8_t salt[ALIRO_SALT_MAX], block[ALIRO_KEY_BLOCK_LEN], enc[32], dec[32], ursk_r[32];
	size_t slen;

	if (aliro_salt_build(ALIRO_SALT_SESSION, txid, reader_group_x, reph_pub + 1, reader_id,
			     ALIRO_IFACE_BLE, 0x0100u, 0x00u, 0x01u, NULL, k_a5, sizeof(k_a5), salt,
			     &slen) != 0 ||
	    aliro_crypto_derive_block(z, salt, slen, device_eph_pub + 1, block) != 0) {
		return -1;
	}
	aliro_crypto_split(block, 1, enc, dec, ursk_r);

	struct aliro_secchan scr;

	aliro_secchan_init(&scr, enc, dec);

	rl = rn;
	if (aliro_apdu_strip_sw(resp, &rl, &sw) != 0 || rl < 16u) {
		return -1;
	}

	uint8_t ptbuf[200];
	size_t ctlen = rl - 16u;
	struct aliro_auth1_response a1;

	if (aliro_secchan_open(&scr, NULL, 0, resp, ctlen, resp + ctlen, ptbuf) != 0 ||
	    aliro_apdu_parse_auth1_response(ptbuf, ctlen, &a1) != 0) {
		return -1;
	}

	uint8_t td2[160];
	size_t td2n;
	const uint8_t *cred_pub = a1.have_device_pub ? a1.device_pub : device_eph_pub;

	if (aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, reader_id, device_eph_pub + 1,
				      reph_pub + 1, txid, td2, sizeof(td2), &td2n) != 0 ||
	    aliro_ecdsa_p256_verify(cred_pub, td2, td2n, a1.device_sig) != 0) {
		return -1;
	}

	/* reader: seal + send EXCHANGE, then open the device's EXCHANGE response */
	uint8_t ex[16], exct[16], extag[16], expayload[64];
	size_t exn;

	if (aliro_apdu_build_exchange(0, 0, 1, ex, sizeof(ex), &exn) != 0 ||
	    aliro_secchan_seal(&scr, NULL, 0, ex, exn, exct, extag) != 0) {
		return -1;
	}
	memcpy(expayload, exct, exn);
	memcpy(expayload + exn, extag, 16);
	if (aliro_apdu_wrap(ALIRO_INS_EXCHANGE, expayload, exn + 16u, apdu, sizeof(apdu), &an) !=
		    0 ||
	    aliro_device_on_command(&dev, apdu, an, resp, sizeof(resp), &rn) != 0) {
		return -1;
	}

	rl = rn;
	if (aliro_apdu_strip_sw(resp, &rl, &sw) != 0 || rl < 16u) {
		return -1;
	}

	uint8_t body[32];
	size_t bodylen = rl - 16u;

	if (aliro_secchan_open(&scr, NULL, 0, resp, bodylen, resp + bodylen, body) != 0) {
		return -1;
	}

	int ok = bodylen >= 4u && body[2] == 0x00u && body[3] == 0x00u;

	/* the money check: both sides independently derived the same URSK */
	return (ok && memcmp(ursk_r, dev.ursk, 32) == 0) ? 0 : -1;
}
#endif

/* The full self-test body, callable from the host main() below and from the
 * on-target app_main() (ports/esp32/test/on_target_ec), which runs the same
 * suite against the real PSA P-256 backend instead of the host fake curve. */
int aliro_device_selftest(void)
{
	printf("== aliro_device: initiator-side codec + crypto ==\n");

	test_codec();
	test_cryptogram();
	test_secchan();
	test_key_schedule();
	test_blesk_channel();

	printf("\n== full standard-path loopback ==\n");
#if defined(ALIRO_DEVICE_HAVE_EC)
	T_OK("loopback: reader URSK == device URSK", loopback() == 0);
#else
	printf("  SKIP loopback (no host EC double; runs on target with real aliro_prim)\n");
#endif

	if (fails) {
		printf("\nRESULT: %d FAIL\n", fails);
		return 1;
	}
	printf("\nRESULT: PASS\n");
	return 0;
}

/* Host entry. On a firmware target the on-target main()/app_main() calls
 * aliro_device_selftest() instead: ESP-IDF defines ESP_PLATFORM, Zephyr (nRF)
 * defines __ZEPHYR__ and supplies its own main(), so exclude this there. */
#if !defined(ESP_PLATFORM) && !defined(__ZEPHYR__)
int main(void)
{
	return aliro_device_selftest();
}
#endif
