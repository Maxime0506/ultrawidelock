// Unit tests for libaliro_presence: config, key load, framing, verification, the
// TTL cache, and the full challenge/verify flow driven over a socketpair standing
// in for the dongle. No hardware, no PAM. Built + run by run.sh (also under ASan).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "aliro_assert.h"
#include "aliro_presence.h"

static int checks;
static int fails;

#define CHECK(name, cond)                                                                          \
	do {                                                                                       \
		checks++;                                                                          \
		if (!(cond)) {                                                                      \
			fails++;                                                                    \
			printf("  FAIL %-28s (%s:%d)\n", (name), __FILE__, __LINE__);               \
		}                                                                                  \
	} while (0)

static const uint8_t k_key[ALIRO_ASSERT_KEY_LEN] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
};

// Builds a signed assertion frame with the given fields into wire[70].
static void make_frame(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint16_t dist,
		       const uint8_t *cred_id_or_null, uint8_t status, uint8_t *wire)
{
	struct aliro_assert a;
	memset(&a, 0, sizeof(a));
	a.status = status;
	memcpy(a.nonce, nonce, ALIRO_ASSERT_NONCE_LEN);
	if (cred_id_or_null != NULL) {
		memcpy(a.cred_id, cred_id_or_null, ALIRO_ASSERT_CREDID_LEN);
	}
	a.distance_cm = dist;
	a.uptime_ms = 500000;
	aliro_assert_build(k_key, &a, wire, ALIRO_ASSERT_WIRE_HMAC, NULL);
}

// Writes data to a fresh temp file and returns its path (static buffer per call
// slot). Returns NULL on failure.
static char *temp_with(const void *data, size_t len)
{
	static char paths[4][64];
	static int slot;
	char *p = paths[slot++ & 3];

	snprintf(p, 64, "/tmp/aliro_presence_XXXXXX");
	int fd = mkstemp(p);
	if (fd < 0) {
		return NULL;
	}
	if (len > 0 && write(fd, data, len) != (ssize_t)len) {
		close(fd);
		return NULL;
	}
	close(fd);
	return p;
}

static void test_config(void)
{
	struct presence_config c;
	presence_config_defaults(&c);
	CHECK("cfg.default_threshold", c.threshold_cm == 40);
	CHECK("cfg.default_ttl", c.cache_ttl_s == 30);
	CHECK("cfg.default_no_cred", c.have_cred_id == 0);

	CHECK("cfg.blank", presence_config_parse_line(&c, "   ") == 0);
	CHECK("cfg.comment", presence_config_parse_line(&c, "# hi") == 0);
	CHECK("cfg.device", presence_config_parse_line(&c, "device = /dev/ttyACM0") == 0);
	CHECK("cfg.device_val", strcmp(c.device, "/dev/ttyACM0") == 0);
	CHECK("cfg.threshold", presence_config_parse_line(&c, "threshold_cm=25") == 0);
	CHECK("cfg.threshold_val", c.threshold_cm == 25);
	CHECK("cfg.ttl", presence_config_parse_line(&c, "cache_ttl_s = 60") == 0);
	CHECK("cfg.ttl_val", c.cache_ttl_s == 60);
	CHECK("cfg.timeout", presence_config_parse_line(&c, "timeout_ms = 1500") == 0);
	CHECK("cfg.timeout_val", c.timeout_ms == 1500);
	CHECK("cfg.cred", presence_config_parse_line(&c, "cred_id = 0011223344556677") == 0);
	CHECK("cfg.cred_flag", c.have_cred_id == 1);
	CHECK("cfg.cred_val", c.cred_id[0] == 0x00 && c.cred_id[7] == 0x77);
	CHECK("cfg.unknown_ok", presence_config_parse_line(&c, "future_key = x") == 0);

	/* malformed values are rejected */
	CHECK("cfg.no_eq", presence_config_parse_line(&c, "device") == -1);
	CHECK("cfg.bad_threshold", presence_config_parse_line(&c, "threshold_cm = huge") == -1);
	CHECK("cfg.threshold_range", presence_config_parse_line(&c, "threshold_cm = 0") == -1);
	CHECK("cfg.bad_cred", presence_config_parse_line(&c, "cred_id = zz") == -1);
	CHECK("cfg.bad_timeout", presence_config_parse_line(&c, "timeout_ms = 50") == -1);

	/* config_load over a temp file */
	const char *body = "device = /dev/x\nthreshold_cm = 30\n# c\ncred_id = aabbccddeeff0011\n";
	char *path = temp_with(body, strlen(body));
	struct presence_config c2;
	presence_config_defaults(&c2);
	CHECK("cfg.load", path && presence_config_load(&c2, path) == 0);
	CHECK("cfg.load_threshold", c2.threshold_cm == 30);
	CHECK("cfg.load_cred", c2.have_cred_id == 1 && c2.cred_id[0] == 0xaa);
	if (path) {
		unlink(path);
	}
	CHECK("cfg.load_missing", presence_config_load(&c2, "/no/such/file") == -1);
}

static void test_key_load(void)
{
	uint8_t k32[ALIRO_ASSERT_KEY_LEN];
	memset(k32, 0x5a, sizeof(k32));
	char *good = temp_with(k32, sizeof(k32));
	uint8_t out[ALIRO_ASSERT_KEY_LEN];
	CHECK("key.load", good && presence_key_load(good, out) == 0);
	CHECK("key.load_val", memcmp(out, k32, sizeof(k32)) == 0);
	if (good) {
		unlink(good);
	}
	char *short_f = temp_with(k32, 10);
	CHECK("key.short", short_f && presence_key_load(short_f, out) == -1);
	if (short_f) {
		unlink(short_f);
	}
	CHECK("key.missing", presence_key_load("/no/such/key", out) == -1);
}

static void test_framing(void)
{
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];
	memset(nonce, 0xAB, sizeof(nonce));
	uint8_t ch[PRESENCE_CHALLENGE_LEN];
	presence_build_challenge(nonce, ch);
	CHECK("chal.magic", ch[0] == 'A' && ch[1] == 'C');
	CHECK("chal.nonce", memcmp(ch + 2, nonce, ALIRO_ASSERT_NONCE_LEN) == 0);

	uint8_t frame[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 20, NULL, ALIRO_PRESENCE_PRESENT, frame);
	/* embed the frame after 5 garbage bytes */
	uint8_t buf[5 + ALIRO_ASSERT_WIRE_HMAC];
	memset(buf, 0x77, 5);
	memcpy(buf + 5, frame, sizeof(frame));
	CHECK("find.offset", presence_find_frame(buf, sizeof(buf)) == 5);
	CHECK("find.short", presence_find_frame(frame, ALIRO_ASSERT_WIRE_HMAC - 1u) == -1);
	uint8_t nomagic[100];
	memset(nomagic, 0x00, sizeof(nomagic));
	CHECK("find.none", presence_find_frame(nomagic, sizeof(nomagic)) == -1);

	uint8_t ks[PRESENCE_KEYSET_LEN];
	presence_build_keyset(k_key, ks);
	CHECK("keyset.magic", ks[0] == 'A' && ks[1] == 'K');
	CHECK("keyset.key", memcmp(ks + 2, k_key, ALIRO_ASSERT_KEY_LEN) == 0);
}

static void test_verify(void)
{
	struct presence_config c;
	presence_config_defaults(&c);
	c.threshold_cm = 40;
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];
	memset(nonce, 0x11, sizeof(nonce));
	uint8_t cred[ALIRO_ASSERT_CREDID_LEN] = { 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7 };

	uint8_t wire[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 25, cred, ALIRO_PRESENCE_PRESENT, wire);
	CHECK("vf.present", presence_verify(&c, k_key, wire, sizeof(wire), nonce, NULL) ==
			     PRESENCE_PRESENT);

	/* bind a matching credential -> still present */
	memcpy(c.cred_id, cred, sizeof(cred));
	c.have_cred_id = 1;
	CHECK("vf.cred_ok", presence_verify(&c, k_key, wire, sizeof(wire), nonce, NULL) ==
			     PRESENCE_PRESENT);
	/* bind a different credential -> E_CRED */
	c.cred_id[0] ^= 0xFF;
	CHECK("vf.cred_bad", presence_verify(&c, k_key, wire, sizeof(wire), nonce, NULL) ==
			      PRESENCE_E_CRED);
	c.have_cred_id = 0;

	/* out of range -> DENIED */
	uint8_t far[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 100, cred, ALIRO_PRESENCE_PRESENT, far);
	CHECK("vf.far", presence_verify(&c, k_key, far, sizeof(far), nonce, NULL) == PRESENCE_DENIED);

	/* wrong key (forged) -> DENIED */
	uint8_t badkey[ALIRO_ASSERT_KEY_LEN];
	memcpy(badkey, k_key, sizeof(badkey));
	badkey[0] ^= 1;
	CHECK("vf.forged", presence_verify(&c, badkey, wire, sizeof(wire), nonce, NULL) ==
			     PRESENCE_DENIED);
}

static void test_transact_and_cache(void)
{
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];
	memset(nonce, 0x22, sizeof(nonce));

	/* happy transact over a socketpair: pre-write the dongle's response. */
	int sv[2];
	CHECK("sp.create", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	uint8_t frame[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 30, NULL, ALIRO_PRESENCE_PRESENT, frame);
	write(sv[1], frame, sizeof(frame));
	uint8_t got[ALIRO_ASSERT_WIRE_MAX];
	size_t got_len = 0;
	CHECK("tx.ok", presence_transact_fd(sv[0], nonce, 1000, got, &got_len) == 0);
	CHECK("tx.len", got_len == ALIRO_ASSERT_WIRE_HMAC);
	CHECK("tx.frame", memcmp(got, frame, sizeof(frame)) == 0);
	close(sv[0]);
	close(sv[1]);

	/* timeout: nothing written, short deadline. */
	int sv2[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
	CHECK("tx.timeout", presence_transact_fd(sv2[0], nonce, 100, got, NULL) == PRESENCE_E_IO);
	close(sv2[0]);
	close(sv2[1]);

	/* TTL cache round-trip. */
	char *stamp = temp_with(NULL, 0);
	CHECK("cache.stamp", stamp && presence_cache_stamp(stamp, 1000) == 0);
	CHECK("cache.fresh", presence_cache_fresh(stamp, 1005, 30) == 1);
	CHECK("cache.expired", presence_cache_fresh(stamp, 1040, 30) == 0);
	CHECK("cache.backwards", presence_cache_fresh(stamp, 999, 30) == 0);
	CHECK("cache.boundary", presence_cache_fresh(stamp, 1030, 30) == 1);
	CHECK("cache.missing", presence_cache_fresh("/no/such/stamp", 1000, 30) == 0);
	if (stamp) {
		unlink(stamp);
	}
}

static void test_check_fd(void)
{
	struct presence_config c;
	presence_config_defaults(&c);
	c.threshold_cm = 40;
	char *stamp = temp_with(NULL, 0);
	snprintf(c.stampfile, sizeof(c.stampfile), "%s", stamp ? stamp : "/tmp/aliro_stamp_x");

	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];
	memset(nonce, 0x33, sizeof(nonce));
	int sv[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	uint8_t frame[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 15, NULL, ALIRO_PRESENCE_PRESENT, frame);
	write(sv[1], frame, sizeof(frame));

	int r = presence_check_fd(sv[0], &c, k_key, nonce, 2000);
	CHECK("checkfd.present", r == PRESENCE_PRESENT);
	/* success must have stamped the cache */
	CHECK("checkfd.stamped", presence_cache_fresh(c.stampfile, 2000, 30) == 1);
	close(sv[0]);
	close(sv[1]);
	if (stamp) {
		unlink(stamp);
	}

	/* absent response -> DENIED, no stamp. */
	char *stamp2 = temp_with(NULL, 0);
	snprintf(c.stampfile, sizeof(c.stampfile), "%s", stamp2 ? stamp2 : "/tmp/aliro_stamp_y");
	int sv2[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
	uint8_t afr[ALIRO_ASSERT_WIRE_HMAC];
	make_frame(nonce, 15, NULL, ALIRO_PRESENCE_ABSENT, afr);
	write(sv2[1], afr, sizeof(afr));
	CHECK("checkfd.absent", presence_check_fd(sv2[0], &c, k_key, nonce, 3000) == PRESENCE_DENIED);
	CHECK("checkfd.nostamp", presence_cache_fresh(c.stampfile, 3000, 30) == 0);
	close(sv2[0]);
	close(sv2[1]);
	if (stamp2) {
		unlink(stamp2);
	}
}

int main(void)
{
	test_config();
	test_key_load();
	test_framing();
	test_verify();
	test_transact_and_cache();
	test_check_fd();

	printf("\n  presence: %d checks, %d failed\n", checks, fails);
	if (fails) {
		printf("  RESULT: FAIL\n");
		return 1;
	}
	printf("  RESULT: PASS\n");
	return 0;
}
