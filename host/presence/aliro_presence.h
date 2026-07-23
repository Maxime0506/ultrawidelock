/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * libaliro_presence — host side of the presence second factor.
 *
 * A PAM module (or any host program) challenges the USB presence dongle and
 * decides whether a provisioned iPhone is within the configured distance. All
 * the logic is here and is transport-testable: the serial I/O operates on a
 * caller-supplied fd, so a socketpair stands in for the dongle in unit tests;
 * frame parsing, config, the pairing key, verification, and the freshness cache
 * are all pure functions. pam_aliro.c is only a thin shim over presence_check().
 *
 * Threat model: the USB link is hostile (a spoofed serial device must not be
 * able to assert presence -> HMAC-authenticated frames, aliro_assert.c). The
 * pairing key file and the cache stamp are root-owned 0600; an attacker who is
 * already root has won regardless, so the stamp is a plain monotonic timestamp.
 * This is a SECOND FACTOR (distance-bounded presence), never sole auth.
 */
#ifndef ALIRO_PRESENCE_H
#define ALIRO_PRESENCE_H

#include <stddef.h>
#include <stdint.h>

#include "aliro_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRESENCE_PATH_MAX 256u
#define PRESENCE_CHALLENGE_LEN 18u /* 'A' 'C' | nonce(16) */
#define PRESENCE_KEYSET_LEN 34u    /* 'A' 'K' | key(32): load the pairing key */

struct presence_config {
	char device[PRESENCE_PATH_MAX];    /* serial tty, e.g. /dev/tty.usbmodem* */
	char keyfile[PRESENCE_PATH_MAX];   /* 32 raw bytes, root-only */
	char stampfile[PRESENCE_PATH_MAX]; /* TTL cache stamp, root-only */
	uint16_t threshold_cm;             /* inclusive distance gate (default 40) */
	uint32_t cache_ttl_s;              /* presence cache window (default 30) */
	uint32_t timeout_ms;               /* transaction deadline (default 3000) */
	uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN]; /* required credential, if bound */
	int have_cred_id;                  /* 0 = accept any authentic in-range cred */
};

/* Verdicts from presence_verify / presence_check_fd (mirrors aliro_assert plus a
 * credential-allowlist reject). >=0 useful; <0 error. */
enum presence_result {
	PRESENCE_PRESENT = 1,    /* authentic, in range, cred allowed */
	PRESENCE_DENIED = 0,     /* a clean reject (absent/out-of-range/etc.) */
	PRESENCE_E_CRED = -10,   /* authentic + in range but wrong credential */
	PRESENCE_E_IO = -11,     /* serial read/write / timeout / short frame */
	PRESENCE_E_CONFIG = -12, /* bad config / missing key file */
};

/* ---- config ------------------------------------------------------------- */
void presence_config_defaults(struct presence_config *c);
/* Apply one "key = value" line (comments '#', blank lines ignored). Returns 0 on
 * success (including ignored lines), -1 on a malformed value. */
int presence_config_parse_line(struct presence_config *c, const char *line);
/* Load + parse a config file over the defaults. -1 if the file cannot be read. */
int presence_config_load(struct presence_config *c, const char *path);

/* ---- pairing key -------------------------------------------------------- */
/* Read exactly 32 raw key bytes from path. -1 on any error (missing/short). */
int presence_key_load(const char *path, uint8_t key[ALIRO_ASSERT_KEY_LEN]);

/* ---- wire framing ------------------------------------------------------- */
/* Build the 18-byte challenge for a nonce. buf must hold PRESENCE_CHALLENGE_LEN. */
void presence_build_challenge(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint8_t *buf);
/* Build the 34-byte key-load frame (pairing the dongle to the host key). buf must
 * hold PRESENCE_KEYSET_LEN. */
void presence_build_keyset(const uint8_t key[ALIRO_ASSERT_KEY_LEN], uint8_t *buf);
/* Find a complete 70-byte assertion frame (synced on its 0xA1 0x50 magic) in a
 * byte buffer. Returns the start offset, or -1 if no complete frame is present.
 * Tolerates leading garbage/desync on the serial line. */
long presence_find_frame(const uint8_t *buf, size_t len);

/* ---- transport (any fd; socketpair in tests) ---------------------------- */
/* Write the challenge to fd, then read until a full assertion frame arrives or
 * timeout_ms elapses. On success writes 70 bytes to wire and returns 0; returns
 * PRESENCE_E_IO on timeout/short/error. */
int presence_transact_fd(int fd, const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint32_t timeout_ms,
			 uint8_t wire[ALIRO_ASSERT_WIRE_LEN]);

/* ---- verification ------------------------------------------------------- */
/* Verify a received frame against config + key + the nonce we sent + a monotonic
 * min_uptime floor (0 to skip). Returns a presence_result. *out (optional) gets
 * the parsed assertion for logging. */
int presence_verify(const struct presence_config *c, const uint8_t key[ALIRO_ASSERT_KEY_LEN],
		    const uint8_t *wire, size_t wire_len,
		    const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], struct aliro_assert *out);

/* ---- freshness cache ---------------------------------------------------- */
/* 1 if path holds a timestamp within ttl_s of now_s (presence still fresh), else
 * 0 (stale/missing/unreadable). */
int presence_cache_fresh(const char *path, uint64_t now_s, uint32_t ttl_s);
/* Write now_s to path (0600). -1 on error. */
int presence_cache_stamp(const char *path, uint64_t now_s);

/* ---- integration -------------------------------------------------------- */
/* Full check on an already-open fd (challenge -> verify -> stamp on success).
 * Injectable nonce + now for tests. Returns a presence_result (>=1 present). */
int presence_check_fd(int fd, const struct presence_config *c,
		      const uint8_t key[ALIRO_ASSERT_KEY_LEN],
		      const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint64_t now_s);

/* Full check the PAM module calls: honour the TTL cache, else open the tty and
 * run presence_check_fd. Returns a presence_result. */
int presence_check(const struct presence_config *c);

#ifdef __cplusplus
}
#endif

#endif /* ALIRO_PRESENCE_H */
