// Host side of the presence second factor (see aliro_presence.h): config, pairing
// key, wire framing, fd transport, verification, and the freshness cache. All the
// decision logic lives here so the PAM shim is trivial and everything is testable
// against a socketpair + temp files.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "aliro_presence.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- small helpers ------------------------------------------------------ */

// Trims leading/trailing ASCII whitespace in place, returning a pointer into s.
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
		s++;
	}
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
		*--e = '\0';
	}
	return s;
}

static int hexnib(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

// Decodes an even-length hex string of exactly want bytes. 0 on success, -1 else.
static int hexdecode(const char *s, uint8_t *out, size_t want)
{
	if (strlen(s) != want * 2u) {
		return -1;
	}
	for (size_t i = 0; i < want; i++) {
		int hi = hexnib(s[2u * i]);
		int lo = hexnib(s[2u * i + 1u]);
		if (hi < 0 || lo < 0) {
			return -1;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

// Fills out with len CSPRNG bytes from /dev/urandom. 0 on success, -1 on error.
static int rand_bytes(uint8_t *out, size_t len)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		return -1;
	}
	size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, out + got, len - got);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		got += (size_t)n;
	}
	close(fd);
	return 0;
}

// Current wall-clock time in seconds (reboot-stable, unlike CLOCK_MONOTONIC).
static uint64_t now_seconds(void)
{
	return (uint64_t)time(NULL);
}

// Milliseconds from a monotonic clock, for the transaction deadline.
static uint64_t mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

// Opens the serial tty in raw 8N1 mode. Returns the fd, or -1 on error.
static int open_tty(const char *path)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		return -1;
	}
	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		cfsetispeed(&t, B115200);
		cfsetospeed(&t, B115200);
		t.c_cc[VMIN] = 0;
		t.c_cc[VTIME] = 0;
		(void)tcsetattr(fd, TCSANOW, &t);
	}
	return fd;
}

// Writes exactly len bytes to fd, retrying short writes. 0 on success, -1 else.
static int write_all(int fd, const uint8_t *buf, size_t len)
{
	size_t sent = 0;

	while (sent < len) {
		ssize_t n = write(fd, buf + sent, len - sent);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return -1;
		}
		sent += (size_t)n;
	}
	return 0;
}

/* ---- config ------------------------------------------------------------- */

void presence_config_defaults(struct presence_config *c)
{
	memset(c, 0, sizeof(*c));
	snprintf(c->device, sizeof(c->device), "%s", "/dev/aliro-presence");
	snprintf(c->keyfile, sizeof(c->keyfile), "%s", "/etc/aliro-presence/key");
	snprintf(c->stampfile, sizeof(c->stampfile), "%s", "/run/aliro-presence.stamp");
	c->threshold_cm = 40;
	c->cache_ttl_s = 30;
	c->timeout_ms = 3000;
	c->have_cred_id = 0;
}

int presence_config_parse_line(struct presence_config *c, const char *line)
{
	char buf[512];

	snprintf(buf, sizeof(buf), "%s", line);
	char *s = trim(buf);

	if (*s == '\0' || *s == '#') {
		return 0; /* blank / comment */
	}
	char *eq = strchr(s, '=');
	if (eq == NULL) {
		return -1;
	}
	*eq = '\0';
	char *key = trim(s);
	char *val = trim(eq + 1);

	if (strcmp(key, "device") == 0) {
		snprintf(c->device, sizeof(c->device), "%s", val);
	} else if (strcmp(key, "keyfile") == 0) {
		snprintf(c->keyfile, sizeof(c->keyfile), "%s", val);
	} else if (strcmp(key, "stampfile") == 0) {
		snprintf(c->stampfile, sizeof(c->stampfile), "%s", val);
	} else if (strcmp(key, "threshold_cm") == 0) {
		char *end;
		long v = strtol(val, &end, 10);
		if (*end != '\0' || v < 1 || v > 65534) {
			return -1;
		}
		c->threshold_cm = (uint16_t)v;
	} else if (strcmp(key, "cache_ttl_s") == 0) {
		char *end;
		long v = strtol(val, &end, 10);
		if (*end != '\0' || v < 0 || v > 86400) {
			return -1;
		}
		c->cache_ttl_s = (uint32_t)v;
	} else if (strcmp(key, "timeout_ms") == 0) {
		char *end;
		long v = strtol(val, &end, 10);
		if (*end != '\0' || v < 100 || v > 60000) {
			return -1;
		}
		c->timeout_ms = (uint32_t)v;
	} else if (strcmp(key, "cred_id") == 0) {
		if (hexdecode(val, c->cred_id, ALIRO_ASSERT_CREDID_LEN) != 0) {
			return -1;
		}
		c->have_cred_id = 1;
	}
	/* Unknown keys are ignored (forward-compat). */
	return 0;
}

int presence_config_load(struct presence_config *c, const char *path)
{
	FILE *f = fopen(path, "r");

	if (f == NULL) {
		return -1;
	}
	char line[512];
	int rc = 0;

	while (fgets(line, sizeof(line), f) != NULL) {
		if (presence_config_parse_line(c, line) != 0) {
			rc = -1; /* keep going, but report a malformed line */
		}
	}
	fclose(f);
	return rc;
}

/* ---- pairing key -------------------------------------------------------- */

int presence_key_load(const char *path, uint8_t key[ALIRO_ASSERT_KEY_LEN])
{
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		return -1;
	}
	size_t got = 0;
	while (got < ALIRO_ASSERT_KEY_LEN) {
		ssize_t n = read(fd, key + got, ALIRO_ASSERT_KEY_LEN - got);
		if (n <= 0) {
			close(fd);
			return -1; /* short / error */
		}
		got += (size_t)n;
	}
	close(fd);
	return 0;
}

/* ---- wire framing ------------------------------------------------------- */

void presence_build_challenge(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint8_t *buf)
{
	buf[0] = 'A';
	buf[1] = 'C';
	memcpy(buf + 2, nonce, ALIRO_ASSERT_NONCE_LEN);
}

void presence_build_keyset(const uint8_t key[ALIRO_ASSERT_KEY_LEN], uint8_t *buf)
{
	buf[0] = 'A';
	buf[1] = 'K';
	memcpy(buf + 2, key, ALIRO_ASSERT_KEY_LEN);
}

long presence_find_frame(const uint8_t *buf, size_t len)
{
	if (buf == NULL) {
		return -1;
	}
	/* Frames are variable length -- the alg byte inside the signed prefix says
	 * which -- so a magic match is only a frame once that many bytes have
	 * actually arrived. Scanning on magic alone would hand a half-received
	 * P-256 frame to the verifier as if it were complete. */
	for (size_t i = 0; i + ALIRO_ASSERT_SIGNED_LEN <= len; i++) {
		if (buf[i] != 0xA1u || buf[i + 1u] != 0x50u) {
			continue;
		}
		size_t need = aliro_assert_wire_len(aliro_assert_peek_alg(buf + i, len - i));

		if (need != 0u && i + need <= len) {
			return (long)i;
		}
	}
	return -1;
}

/* ---- transport ---------------------------------------------------------- */

int presence_transact_fd(int fd, const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint32_t timeout_ms,
			 uint8_t wire[ALIRO_ASSERT_WIRE_MAX], size_t *wire_len)
{
	uint8_t challenge[PRESENCE_CHALLENGE_LEN];

	presence_build_challenge(nonce, challenge);
	if (write_all(fd, challenge, sizeof(challenge)) != 0) {
		return PRESENCE_E_IO;
	}

	/* Accumulate until a full assertion frame syncs on its magic, or we run out
	 * of time. A small ceiling bounds the buffer against a chatty/garbage line. */
	uint8_t acc[512];
	size_t used = 0;
	uint64_t deadline = mono_ms() + timeout_ms;

	for (;;) {
		uint64_t nowm = mono_ms();
		if (nowm >= deadline) {
			return PRESENCE_E_IO; /* timeout */
		}
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int pr = poll(&pfd, 1, (int)(deadline - nowm));
		if (pr < 0) {
			if (errno == EINTR) {
				continue;
			}
			return PRESENCE_E_IO;
		}
		if (pr == 0) {
			return PRESENCE_E_IO; /* timeout */
		}
		if (used == sizeof(acc)) {
			/* Buffer full without a frame: drop the oldest half and resync. */
			memmove(acc, acc + sizeof(acc) / 2, sizeof(acc) / 2);
			used = sizeof(acc) / 2;
		}
		ssize_t n = read(fd, acc + used, sizeof(acc) - used);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return PRESENCE_E_IO;
		}
		if (n == 0) {
			return PRESENCE_E_IO; /* peer closed */
		}
		used += (size_t)n;

		long off = presence_find_frame(acc, used);
		if (off >= 0) {
			/* find_frame only returns an offset once the whole frame is
			 * present, so this length is always fully buffered. */
			size_t n_frame = aliro_assert_wire_len(
				aliro_assert_peek_alg(acc + off, used - (size_t)off));

			memcpy(wire, acc + off, n_frame);
			if (wire_len != NULL) {
				*wire_len = n_frame;
			}
			return 0;
		}
	}
}

/* ---- verification ------------------------------------------------------- */

int presence_verify(const struct presence_config *c, const uint8_t key[ALIRO_ASSERT_KEY_LEN],
		    const uint8_t *wire, size_t wire_len,
		    const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], struct aliro_assert *out)
{
	struct aliro_assert a;
	int v = aliro_assert_verify(key, wire, wire_len, nonce, c->threshold_cm, 0, &a);

	if (out != NULL) {
		*out = a;
	}
	if (v != ALIRO_ASSERT_OK) {
		return PRESENCE_DENIED; /* a clean not-present (reason in *out) */
	}
	if (c->have_cred_id &&
	    memcmp(a.cred_id, c->cred_id, ALIRO_ASSERT_CREDID_LEN) != 0) {
		return PRESENCE_E_CRED; /* authentic + in range, wrong credential */
	}
	return PRESENCE_PRESENT;
}

/* ---- freshness cache ---------------------------------------------------- */

int presence_cache_fresh(const char *path, uint64_t now_s, uint32_t ttl_s)
{
	FILE *f = fopen(path, "r");

	if (f == NULL) {
		return 0;
	}
	unsigned long long stamp = 0;
	int ok = (fscanf(f, "%llu", &stamp) == 1);
	fclose(f);
	if (!ok) {
		return 0;
	}
	/* Fresh only if now is within [stamp, stamp+ttl]. A clock moved backwards
	 * (now < stamp) reads as stale, never as an extended window. */
	if (now_s < stamp) {
		return 0;
	}
	return (now_s - stamp) <= ttl_s;
}

int presence_cache_stamp(const char *path, uint64_t now_s)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	if (fd < 0) {
		return -1;
	}
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)now_s);
	int rc = (len > 0 && write_all(fd, (const uint8_t *)buf, (size_t)len) == 0) ? 0 : -1;

	close(fd);
	return rc;
}

/* ---- integration -------------------------------------------------------- */

int presence_check_fd(int fd, const struct presence_config *c,
		      const uint8_t key[ALIRO_ASSERT_KEY_LEN],
		      const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN], uint64_t now_s)
{
	uint8_t wire[ALIRO_ASSERT_WIRE_MAX];
	size_t wire_len = 0;
	int rc = presence_transact_fd(fd, nonce, c->timeout_ms, wire, &wire_len);

	if (rc != 0) {
		return PRESENCE_E_IO;
	}
	struct aliro_assert a;
	int v = presence_verify(c, key, wire, wire_len, nonce, &a);

	if (v == PRESENCE_PRESENT) {
		(void)presence_cache_stamp(c->stampfile, now_s);
	}
	return v;
}

int presence_check(const struct presence_config *c)
{
	uint64_t now = now_seconds();

	if (presence_cache_fresh(c->stampfile, now, c->cache_ttl_s)) {
		return PRESENCE_PRESENT; /* within the cached window: no transaction */
	}

	uint8_t key[ALIRO_ASSERT_KEY_LEN];
	if (presence_key_load(c->keyfile, key) != 0) {
		return PRESENCE_E_CONFIG;
	}
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];
	if (rand_bytes(nonce, sizeof(nonce)) != 0) {
		return PRESENCE_E_IO;
	}
	int fd = open_tty(c->device);
	if (fd < 0) {
		return PRESENCE_E_IO;
	}
	int rc = presence_check_fd(fd, c, key, nonce, now);

	close(fd);
	return rc;
}
