/**
 * @file woz_report.c — range report line codec (implementation).
 *
 * No snprintf and no sscanf anywhere in this file. Both pull in C library
 * behaviour that varies with Kconfig on the targets this has to run on, and the
 * failure mode is a silently malformed line rather than a build error. Decimal
 * conversion here is 30 lines and cannot be configured away.
 *
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */

#include "woz_report.h"

#include <errno.h>
#include <string.h>

#define MAGIC_LEN 4

uint16_t woz_report_crc16(const char *data, size_t len)
{
	uint16_t crc = 0xFFFFu; /* CCITT-FALSE init */
	size_t i;
	int b;

	if (data == NULL) {
		return 0u;
	}
	for (i = 0u; i < len; i++) {
		crc ^= (uint16_t)((uint8_t)data[i]) << 8;
		for (b = 0; b < 8; b++) {
			crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
					      : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

/* Append an unsigned 64-bit value in decimal. Returns bytes written, or 0 if it
 * would not fit; the caller checks once at the end rather than at every field,
 * because a partial line is never emitted. */
static size_t put_u64(char *out, size_t cap, uint64_t v)
{
	char tmp[20];
	size_t n = 0u;
	size_t i;

	if (v == 0u) {
		if (cap < 1u) {
			return 0u;
		}
		out[0] = '0';
		return 1u;
	}
	while (v != 0u && n < sizeof(tmp)) {
		tmp[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	}
	if (n > cap) {
		return 0u;
	}
	for (i = 0u; i < n; i++) {
		out[i] = tmp[n - 1u - i];
	}
	return n;
}

static size_t put_i32(char *out, size_t cap, int32_t v)
{
	uint64_t mag;
	size_t n = 0u;

	if (v < 0) {
		if (cap < 1u) {
			return 0u;
		}
		out[0] = '-';
		n = 1u;
		/* Negate in 64-bit: -(int32_t)INT32_MIN overflows in 32. */
		mag = (uint64_t)(-(int64_t)v);
	} else {
		mag = (uint64_t)v;
	}
	{
		size_t k = put_u64(out + n, cap - n, mag);

		if (k == 0u) {
			return 0u;
		}
		return n + k;
	}
}

static size_t put_char(char *out, size_t cap, char c)
{
	if (cap < 1u) {
		return 0u;
	}
	out[0] = c;
	return 1u;
}

static size_t put_hex4(char *out, size_t cap, uint16_t v)
{
	static const char k_hex[] = "0123456789ABCDEF";
	int i;

	if (cap < 4u) {
		return 0u;
	}
	for (i = 0; i < 4; i++) {
		out[i] = k_hex[(v >> (12 - 4 * i)) & 0xFu];
	}
	return 4u;
}

/* Every append goes through this so a single overflow check at the end is
 * enough: once `fail` is set nothing more is written. */
struct app {
	char *buf;
	size_t cap;
	size_t len;
	bool fail;
};

static void app_u64(struct app *a, uint64_t v)
{
	size_t n;

	if (a->fail) {
		return;
	}
	n = put_u64(a->buf + a->len, a->cap - a->len, v);
	if (n == 0u) {
		a->fail = true;
		return;
	}
	a->len += n;
}

static void app_i32(struct app *a, int32_t v)
{
	size_t n;

	if (a->fail) {
		return;
	}
	n = put_i32(a->buf + a->len, a->cap - a->len, v);
	if (n == 0u) {
		a->fail = true;
		return;
	}
	a->len += n;
}

static void app_ch(struct app *a, char c)
{
	size_t n;

	if (a->fail) {
		return;
	}
	n = put_char(a->buf + a->len, a->cap - a->len, c);
	if (n == 0u) {
		a->fail = true;
		return;
	}
	a->len += n;
}

static void app_str(struct app *a, const char *s)
{
	while (*s != '\0' && !a->fail) {
		app_ch(a, *s++);
	}
}

int woz_report_format(const struct woz_range_report *r, char *out, size_t cap)
{
	struct app a = {out, cap, 0u, false};
	uint16_t crc;
	size_t n;

	if (r == NULL || out == NULL) {
		return -EINVAL;
	}

	app_str(&a, WOZ_REPORT_MAGIC);
	app_ch(&a, ' ');
	app_u64(&a, r->anchor_id);
	app_ch(&a, ' ');
	app_u64(&a, r->seq);
	app_ch(&a, ' ');
	app_u64(&a, (uint32_t)(r->us >> 32));
	app_ch(&a, ' ');
	app_u64(&a, (uint32_t)(r->us & 0xFFFFFFFFu));
	app_ch(&a, ' ');
	app_i32(&a, r->d_mm);
	app_ch(&a, ' ');
	app_u64(&a, r->quality);
	app_ch(&a, ' ');
	app_u64(&a, r->trust);
	app_ch(&a, ' ');
	app_u64(&a, r->flags);
	app_ch(&a, ' ');
	app_u64(&a, r->dropped);
	app_ch(&a, ' ');
	app_u64(&a, r->accepted);
	if (a.fail) {
		return -ENOSPC;
	}

	/* The CRC covers everything emitted so far, which is exactly what the
	 * parser recomputes: no ambiguity about where the covered range ends. */
	crc = woz_report_crc16(out, a.len);
	app_ch(&a, ' ');
	app_ch(&a, '*');
	if (a.fail) {
		return -ENOSPC;
	}
	n = put_hex4(a.buf + a.len, a.cap - a.len, crc);
	if (n == 0u) {
		return -ENOSPC;
	}
	a.len += n;
	app_ch(&a, '\n');
	if (a.fail) {
		return -ENOSPC;
	}
	return (int)a.len;
}

/* ---- parsing ---- */

struct scan {
	const char *p;
	const char *end;
	bool fail;
};

static void skip_space(struct scan *s)
{
	while (s->p < s->end && *s->p == ' ') {
		s->p++;
	}
}

/* Reads one decimal field. Rejects an empty field and anything that would
 * exceed `limit`, so a corrupted line cannot wrap a counter into a plausible
 * small number. */
static uint64_t scan_u64(struct scan *s, uint64_t limit)
{
	uint64_t v = 0u;
	int digits = 0;

	if (s->fail) {
		return 0u;
	}
	skip_space(s);
	while (s->p < s->end && *s->p >= '0' && *s->p <= '9') {
		v = v * 10u + (uint64_t)(*s->p - '0');
		if (v > limit) {
			s->fail = true;
			return 0u;
		}
		s->p++;
		digits++;
		if (digits > 20) {
			s->fail = true;
			return 0u;
		}
	}
	if (digits == 0) {
		s->fail = true;
	}
	return v;
}

static int32_t scan_i32(struct scan *s)
{
	bool neg = false;
	uint64_t mag;

	if (s->fail) {
		return 0;
	}
	skip_space(s);
	if (s->p < s->end && *s->p == '-') {
		neg = true;
		s->p++;
	}
	mag = scan_u64(s, neg ? 2147483648ULL : 2147483647ULL);
	if (s->fail) {
		return 0;
	}
	return neg ? (int32_t)(-(int64_t)mag) : (int32_t)mag;
}

int woz_report_parse(const char *line, size_t len, struct woz_range_report *out)
{
	struct woz_range_report r;
	struct scan s;
	uint64_t hi, lo;
	uint16_t want, got;
	size_t covered;
	size_t i;

	if (line == NULL || out == NULL) {
		return -EINVAL;
	}

	/* Trim the line ending, whichever one arrived. */
	while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
		len--;
	}
	if (len < MAGIC_LEN + 6u) {
		return -EBADMSG;
	}

	/*
	 * Refuse an unknown magic rather than reinterpreting it. An ARP2 with
	 * more fields must not be read as an ARP1 that happens to parse.
	 */
	if (memcmp(line, WOZ_REPORT_MAGIC, MAGIC_LEN) != 0) {
		return -EBADMSG;
	}

	/* Find the CRC marker. Searching from the end means a stray '*' inside
	 * the payload cannot truncate the covered range. */
	i = len;
	while (i > 0u && line[i - 1u] != '*') {
		i--;
	}
	if (i == 0u || len - i != 4u) {
		return -EBADMSG;
	}
	/* The marker is preceded by the separating space, which is not covered. */
	if (i < 2u || line[i - 2u] != ' ') {
		return -EBADMSG;
	}
	covered = i - 2u;

	got = 0u;
	for (size_t k = 0u; k < 4u; k++) {
		char c = line[i + k];
		uint16_t nib;

		if (c >= '0' && c <= '9') {
			nib = (uint16_t)(c - '0');
		} else if (c >= 'A' && c <= 'F') {
			nib = (uint16_t)(c - 'A' + 10);
		} else if (c >= 'a' && c <= 'f') {
			nib = (uint16_t)(c - 'a' + 10);
		} else {
			return -EBADMSG;
		}
		got = (uint16_t)((got << 4) | nib);
	}
	want = woz_report_crc16(line, covered);
	if (want != got) {
		return -EBADMSG;
	}

	s.p = line + MAGIC_LEN;
	s.end = line + covered;
	s.fail = false;

	r.anchor_id = (uint16_t)scan_u64(&s, 65535u);
	r.seq = (uint32_t)scan_u64(&s, 4294967295u);
	hi = scan_u64(&s, 4294967295u);
	lo = scan_u64(&s, 4294967295u);
	r.us = (hi << 32) | lo;
	r.d_mm = scan_i32(&s);
	r.quality = (uint16_t)scan_u64(&s, 65535u);
	r.trust = (uint8_t)scan_u64(&s, 255u);
	r.flags = (uint8_t)scan_u64(&s, 255u);
	r.dropped = (uint32_t)scan_u64(&s, 4294967295u);
	r.accepted = (uint32_t)scan_u64(&s, 4294967295u);
	if (s.fail) {
		return -EBADMSG;
	}

	/* Trailing junk inside the covered range means the line is not what it
	 * claims to be, even though the CRC matched what was sent. */
	skip_space(&s);
	if (s.p != s.end) {
		return -EBADMSG;
	}

	*out = r;
	return 0;
}
