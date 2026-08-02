/** @file test_aliro_prov.c — reader identity + trust store (de)serialisation KAT.
 *
 * Backs the `aliro-export` / `aliro-import` clone path: a blob written on one
 * board must round-trip byte-for-byte into the same identity + trust store on a
 * second board, and a malformed or wrong-version blob must be rejected, never
 * half-applied. Also locks the wire header so the format cannot drift silently.
 */
#include <string.h>

#include "aliro_prov.h"
#include "test.h"

/* A deterministic, fully-known non-dev identity + trust store to serialise. */
static void make_identity(struct aliro_reader_identity *id, struct aliro_trust_store *ts)
{
	memset(id, 0, sizeof(*id));
	for (unsigned i = 0; i < ALIRO_READER_ID_LEN; i++) {
		id->reader_id[i] = (uint8_t)i;
	}
	for (unsigned i = 0; i < ALIRO_READER_PRIV_LEN; i++) {
		id->sign_priv[i] = (uint8_t)(0x20u + i);
	}
	for (unsigned i = 0; i < ALIRO_GRK_LEN; i++) {
		id->grk[i] = (uint8_t)(0x40u + i);
	}
	id->is_dev = false;

	memset(ts, 0, sizeof(*ts));
	/* Two credentials; only the first carries a Kpersistent. */
	uint8_t a[ALIRO_CRED_PUB_LEN];
	uint8_t b[ALIRO_CRED_PUB_LEN];
	a[0] = 0x04u;
	b[0] = 0x04u;
	for (unsigned i = 1; i < ALIRO_CRED_PUB_LEN; i++) {
		a[i] = (uint8_t)(0x50u + i);
		b[i] = (uint8_t)(0x90u + i);
	}
	T_EQ("add.a", aliro_prov_trust_add(ts, a), 0);
	T_EQ("add.b", aliro_prov_trust_add(ts, b), 0);
	uint8_t kp[ALIRO_KPERSISTENT_LEN];
	memset(kp, 0xAB, sizeof(kp));
	T_EQ("kp.set", aliro_prov_kpersistent_set(ts, aliro_prov_trust_find(ts, a), kp), 0);
}

void test_aliro_prov(void)
{
	t_group("dev default");
	struct aliro_reader_identity dev_id;
	struct aliro_trust_store dev_ts;
	aliro_prov_dev_default(&dev_id, &dev_ts);
	T_OK("dev.is_dev", dev_id.is_dev);
	T_EQ("dev.trust_empty", dev_ts.count, 0);
	/* dev identity round-trips (empty trust). */
	uint8_t db[ALIRO_PROV_BLOB_MAX];
	size_t dn = 0;
	T_EQ("dev.serialize", aliro_prov_serialize(&dev_id, &dev_ts, db, sizeof(db), &dn), 0);
	struct aliro_reader_identity dev_id2;
	struct aliro_trust_store dev_ts2;
	T_EQ("dev.deserialize", aliro_prov_deserialize(db, dn, &dev_id2, &dev_ts2), 0);
	T_OK("dev.id_match", memcmp(&dev_id, &dev_id2, sizeof(dev_id)) == 0);
	T_OK("dev.ts_match", memcmp(&dev_ts, &dev_ts2, sizeof(dev_ts)) == 0);

	t_group("provisioned round-trip");
	struct aliro_reader_identity id;
	struct aliro_trust_store ts;
	make_identity(&id, &ts);
	uint8_t blob[ALIRO_PROV_BLOB_MAX];
	size_t n = 0;
	T_EQ("serialize", aliro_prov_serialize(&id, &ts, blob, sizeof(blob), &n), 0);

	/* Wire header is locked: magic "APRV", version 3, flags 0 (not dev). */
	T_OK("hdr.magic", n >= 6 && memcmp(blob, "APRV", 4) == 0);
	T_EQ("hdr.version", blob[4], 3);
	T_EQ("hdr.flags_notdev", blob[5], 0);
	/* Exact length for 2 anchors, both with a kpersistent row present. */
	size_t want = 6u + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u +
		      2u * ALIRO_CRED_PUB_LEN + 1u + 2u * ALIRO_KPERSISTENT_LEN;
	T_EQ("hdr.length", n, want);

	struct aliro_reader_identity id2;
	struct aliro_trust_store ts2;
	T_EQ("deserialize", aliro_prov_deserialize(blob, n, &id2, &ts2), 0);
	T_OK("id_match", memcmp(&id, &id2, sizeof(id)) == 0);
	T_OK("ts_match", memcmp(&ts, &ts2, sizeof(ts)) == 0);
	T_EQ("kp_valid", ts2.kp_valid, 0x01); /* only anchor 0 has a Kpersistent */

	t_group("serialize overflow");
	uint8_t small[8];
	T_EQ("too_small", aliro_prov_serialize(&id, &ts, small, sizeof(small), &n), -1);

	t_group("deserialize rejects");
	T_EQ("null", aliro_prov_deserialize(NULL, 10, &id2, &ts2), -1);
	T_EQ("short", aliro_prov_deserialize(blob, 4, &id2, &ts2), -1);
	uint8_t bad[ALIRO_PROV_BLOB_MAX];
	memcpy(bad, blob, want);
	bad[0] = 'X'; /* corrupt magic */
	T_EQ("bad_magic", aliro_prov_deserialize(bad, want, &id2, &ts2), -1);
	memcpy(bad, blob, want);
	bad[4] = 0x09; /* unknown version */
	T_EQ("bad_version", aliro_prov_deserialize(bad, want, &id2, &ts2), -1);
	memcpy(bad, blob, want);
	T_EQ("trailing_byte", aliro_prov_deserialize(bad, want - 1u, &id2, &ts2), -1);
	/* count byte past ALIRO_TRUST_MAX. Its offset = 6 + id + priv + grk. */
	memcpy(bad, blob, want);
	bad[6u + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN] = 0xFF;
	T_EQ("count_overflow", aliro_prov_deserialize(bad, want, &id2, &ts2), -1);

	t_group("version back-compat");
	/* Hand-build a v1 blob (no grk, no kp tail), count=0. */
	uint8_t v1[6u + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + 1u];
	memcpy(v1, "APRV", 4);
	v1[4] = 0x01;
	v1[5] = 0x00;
	for (unsigned i = 0; i < ALIRO_READER_ID_LEN; i++) {
		v1[6u + i] = (uint8_t)(0xA0u + i);
	}
	for (unsigned i = 0; i < ALIRO_READER_PRIV_LEN; i++) {
		v1[6u + ALIRO_READER_ID_LEN + i] = (uint8_t)(0xB0u + i);
	}
	v1[sizeof(v1) - 1u] = 0x00; /* count */
	T_EQ("v1.parse", aliro_prov_deserialize(v1, sizeof(v1), &id2, &ts2), 0);
	uint8_t zero_grk[ALIRO_GRK_LEN] = { 0 };
	T_OK("v1.grk_zeroed", memcmp(id2.grk, zero_grk, ALIRO_GRK_LEN) == 0);
	T_EQ("v1.no_trust", ts2.count, 0);

	/* v2 blob (grk, no kp tail), count=0. */
	uint8_t v2[6u + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u];
	memcpy(v2, "APRV", 4);
	v2[4] = 0x02;
	v2[5] = 0x00;
	memset(v2 + 6, 0x11, ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN);
	v2[sizeof(v2) - 1u] = 0x00; /* count */
	T_EQ("v2.parse", aliro_prov_deserialize(v2, sizeof(v2), &id2, &ts2), 0);
	T_EQ("v2.no_trust", ts2.count, 0);

	t_group("trust store logic");
	struct aliro_trust_store t3;
	memset(&t3, 0, sizeof(t3));
	uint8_t key[ALIRO_CRED_PUB_LEN];
	memset(key, 0, sizeof(key));
	key[0] = 0x04u;
	key[1] = 0x77u;
	T_EQ("check.no_anchors", aliro_prov_trust_check(&t3, key), 1);
	T_EQ("add", aliro_prov_trust_add(&t3, key), 0);
	T_EQ("check.trusted", aliro_prov_trust_check(&t3, key), 0);
	T_EQ("add.dedup", aliro_prov_trust_add(&t3, key), 1);
	uint8_t other[ALIRO_CRED_PUB_LEN];
	memset(other, 0, sizeof(other));
	other[0] = 0x04u;
	other[1] = 0x99u;
	T_EQ("check.rejected", aliro_prov_trust_check(&t3, other), -1);
	uint8_t notpoint[ALIRO_CRED_PUB_LEN];
	memset(notpoint, 0x04, sizeof(notpoint));
	notpoint[0] = 0x02u; /* not an uncompressed point */
	T_EQ("add.not_point", aliro_prov_trust_add(&t3, notpoint), -1);

	/* Fill to ALIRO_TRUST_MAX, then the next add reports full. */
	struct aliro_trust_store full;
	memset(&full, 0, sizeof(full));
	for (unsigned i = 0; i < ALIRO_TRUST_MAX; i++) {
		uint8_t k[ALIRO_CRED_PUB_LEN];
		memset(k, 0, sizeof(k));
		k[0] = 0x04u;
		k[1] = (uint8_t)(0x10u + i);
		T_EQ("fill.add", aliro_prov_trust_add(&full, k), 0);
	}
	uint8_t overflow[ALIRO_CRED_PUB_LEN];
	memset(overflow, 0, sizeof(overflow));
	overflow[0] = 0x04u;
	overflow[1] = 0xEEu;
	/*
	 * A FULL STORE EVICTS, it does not refuse. Refusing locked a re-paired
	 * reader out permanently: stale anchors held every slot, the key the
	 * phone presented could never be added, and the unlock failed one step
	 * after the signature verified. 2 = added, something was dropped.
	 */
	T_EQ("fill.evicts", aliro_prov_trust_add(&full, overflow), 2);
	T_EQ("fill.count_held", (int)full.count, (int)ALIRO_TRUST_MAX);
	T_EQ("fill.newest_present", aliro_prov_trust_check(&full, overflow), 0);

	/* The victim is the OLDEST when nothing has a Kpersistent. */
	uint8_t oldest[ALIRO_CRED_PUB_LEN];
	memset(oldest, 0, sizeof(oldest));
	oldest[0] = 0x04u;
	oldest[1] = 0x10u;
	T_EQ("fill.oldest_gone", aliro_prov_trust_check(&full, oldest), -1);

	/*
	 * A slot that has completed a standard phase outranks one that never
	 * has: Kpersistent only exists where a phone actually authenticated, so
	 * an unused slot is the cheaper thing to lose.
	 */
	struct aliro_trust_store used;
	memset(&used, 0, sizeof(used));
	for (unsigned i = 0; i < ALIRO_TRUST_MAX; i++) {
		uint8_t k[ALIRO_CRED_PUB_LEN];
		memset(k, 0, sizeof(k));
		k[0] = 0x04u;
		k[1] = (uint8_t)(0x40u + i);
		T_EQ("used.add", aliro_prov_trust_add(&used, k), 0);
	}
	/* Slot 0 has been used; slot 1 never has. */
	T_EQ("used.kp_set", aliro_prov_kpersistent_set(&used, 0, key), 0);
	uint8_t newcomer[ALIRO_CRED_PUB_LEN];
	memset(newcomer, 0, sizeof(newcomer));
	newcomer[0] = 0x04u;
	newcomer[1] = 0xABu;
	T_EQ("used.evicts", aliro_prov_trust_add(&used, newcomer), 2);

	uint8_t kept[ALIRO_CRED_PUB_LEN];
	memset(kept, 0, sizeof(kept));
	kept[0] = 0x04u;
	kept[1] = 0x40u; /* the one carrying a Kpersistent */
	T_EQ("used.kept_the_used_one", aliro_prov_trust_check(&used, kept), 0);

	uint8_t dropped[ALIRO_CRED_PUB_LEN];
	memset(dropped, 0, sizeof(dropped));
	dropped[0] = 0x04u;
	dropped[1] = 0x41u; /* never used, so this is the victim */
	T_EQ("used.dropped_the_unused", aliro_prov_trust_check(&used, dropped), -1);

	T_EQ("kp.bad_idx", aliro_prov_kpersistent_set(&full, -1, key), -1);
}
