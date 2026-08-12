/**
 * @file flight_recorder.c
 * Binary flight-recorder format: framed records (magic, metadata, configuration, events, end) with
 * little-endian integers and truncation handling; read/write operations with overflow detection.
 */
/*
 * flight_recorder — trace (de)serialisation + the on-device capture path.
 * See flight_recorder.h for the format rationale. The writer/reader half is
 * portable and pointer-free so the same code builds in the firmware, the host
 * replay, and any offline tool; the CONFIG_WOZ_FLIGHT_RECORDER half adds
 * the RAM-ring capture hooks and the `[FREC]` serial dump.
 */
#include "flight_recorder.h"

#include <string.h>

/* ── little-endian cursor primitives ─────────────────────────────────────── */

/**
 * Write a 1-byte unsigned integer to buffer at offset o and advance o.
 */
static void p8(uint8_t *b, size_t *o, uint8_t v)
{
	b[(*o)++] = v;
}
/**
 * Write a 2-byte little-endian unsigned integer to buffer at offset o and advance o.
 */
static void p16(uint8_t *b, size_t *o, uint16_t v)
{
	b[(*o)++] = (uint8_t)v;
	b[(*o)++] = (uint8_t)(v >> 8);
}
/**
 * Write a 4-byte little-endian unsigned integer to buffer at offset o and advance o.
 */
static void p32(uint8_t *b, size_t *o, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		b[(*o)++] = (uint8_t)(v >> (8 * i));
	}
}
/**
 * Write an 8-byte little-endian unsigned integer to buffer at offset o and advance o.
 */
static void p64(uint8_t *b, size_t *o, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		b[(*o)++] = (uint8_t)(v >> (8 * i));
	}
}
/**
 * Copy n bytes from s to buffer at offset o and advance o.
 */
static void pbytes(uint8_t *b, size_t *o, const uint8_t *s, size_t n)
{
	memcpy(b + *o, s, n);
	*o += n;
}

/**
 * Read and advance a 1-byte unsigned integer from buffer at offset o.
 */
static uint8_t g8(const uint8_t *b, size_t *o)
{
	return b[(*o)++];
}
/**
 * Read and advance a 2-byte little-endian unsigned integer from buffer at offset o.
 */
static uint16_t g16(const uint8_t *b, size_t *o)
{
	uint16_t v = (uint16_t)b[*o] | ((uint16_t)b[*o + 1] << 8);
	*o += 2;
	return v;
}
/**
 * Read and advance a 4-byte little-endian unsigned integer from buffer at offset o.
 */
static uint32_t g32(const uint8_t *b, size_t *o)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		v |= (uint32_t)b[*o + (size_t)i] << (8 * i);
	}
	*o += 4;
	return v;
}
/**
 * Read and advance an 8-byte little-endian unsigned integer from buffer at offset o.
 */
static uint64_t g64(const uint8_t *b, size_t *o)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) {
		v |= (uint64_t)b[*o + (size_t)i] << (8 * i);
	}
	*o += 8;
	return v;
}

/* ── writer ──────────────────────────────────────────────────────────────── */

/* Emit one framed record: [u8 type][u16 payload_len][payload]. Latches overflow
 * (and leaves the buffer at its last complete record) if it would not fit. */
static int fr_emit(fr_writer_t *w, uint8_t type, const uint8_t *payload, size_t plen)
{
	size_t o;

	if (w->overflow) {
		return -1;
	}
	if (plen > 0xFFFFu || w->len + 3u + plen > w->cap) {
		w->overflow = true;
		return -1;
	}
	o = w->len;
	p8(w->buf, &o, type);
	p16(w->buf, &o, (uint16_t)plen);
	pbytes(w->buf, &o, payload, plen);
	w->len = o;
	return 0;
}

/**
 * Initialize a flight-recorder writer with an output buffer; write the magic prefix if capacity
 * permits, otherwise latch overflow flag.
 */
void fr_writer_init(fr_writer_t *w, uint8_t *buf, size_t cap)
{
	size_t o = 0;

	w->buf = buf;
	w->cap = cap;
	w->len = 0;
	w->overflow = false;
	if (cap < 4u) { /* no room for even the magic prefix */
		w->overflow = true;
		return;
	}
	p32(buf, &o, FR_MAGIC);
	w->len = o;
}

/**
 * Emit a META record containing flight-recorder version, host port, and optional commit SHA; return
 * 0 on success or -1 on buffer overflow.
 */
int fr_write_meta(fr_writer_t *w, uint16_t port, const char *sha)
{
	uint8_t pl[3 + FR_SHA_MAX];
	size_t o = 0;
	size_t n = sha ? strlen(sha) : 0;

	if (n > FR_SHA_MAX) {
		n = FR_SHA_MAX;
	}
	p16(pl, &o, (uint16_t)FR_VERSION);
	p16(pl, &o, port);
	p8(pl, &o, (uint8_t)n);
	pbytes(pl, &o, (const uint8_t *)sha, n);
	return fr_emit(w, FR_REC_META, pl, o);
}

/**
 * Emit a CONFIG record containing Aliro session parameters and UWB radio configuration; truncate
 * URSK and radio controller data to maximum lengths if needed; return 0 on success or -1 on buffer
 * overflow.
 */
int fr_write_config(fr_writer_t *w, const struct fr_config *c)
{
	uint8_t pl[25 + FR_URSK_LEN + 2 + FR_RC_MAX];
	size_t o = 0;
	uint16_t rc_len = c->rc_len > FR_RC_MAX ? FR_RC_MAX : c->rc_len;

	p32(pl, &o, c->session_id);
	p8(pl, &o, c->channel);
	p8(pl, &o, c->sync_code_index);
	p16(pl, &o, c->slot_duration_rstu);
	p32(pl, &o, c->block_duration_ms);
	p8(pl, &o, c->slot_per_round);
	p32(pl, &o, c->sts_index0);
	p64(pl, &o, c->uwb_time_us);
	pbytes(pl, &o, c->ursk, FR_URSK_LEN);
	p16(pl, &o, rc_len);
	pbytes(pl, &o, c->rc, rc_len);
	return fr_emit(w, FR_REC_CONFIG, pl, o);
}

/**
 * Emit an EV record containing DW3000 register snapshot and received frame data; truncate frame to
 * maximum length if needed; return 0 on success or -1 on buffer overflow.
 */
int fr_write_ev(fr_writer_t *w, const struct fr_ev *e)
{
	uint8_t pl[36 + FR_FRAME_MAX];
	size_t o = 0;
	uint16_t fl = e->frame_len > FR_FRAME_MAX ? FR_FRAME_MAX : e->frame_len;

	p8(pl, &o, e->ep);
	p32(pl, &o, e->status);
	p16(pl, &o, e->datalength);
	p64(pl, &o, e->rx_ts40);
	p64(pl, &o, e->tx_ts40);
	p32(pl, &o, e->systime);
	p8(pl, &o, e->stsq_valid);
	p16(pl, &o, (uint16_t)e->stsq_val);
	p32(pl, &o, (uint32_t)e->stsq_ret);
	p16(pl, &o, fl);
	pbytes(pl, &o, e->frame, fl);
	return fr_emit(w, FR_REC_EV, pl, o);
}

/**
 * Emit an END record with event count and truncation flag; return 0 on success or -1 on buffer
 * overflow.
 */
int fr_write_end(fr_writer_t *w, uint32_t n_events, bool truncated)
{
	uint8_t pl[5];
	size_t o = 0;

	p32(pl, &o, n_events);
	p8(pl, &o, truncated ? 1u : 0u);
	return fr_emit(w, FR_REC_END, pl, o);
}

/* ── reader ──────────────────────────────────────────────────────────────── */

/**
 * Initialize a flight-recorder reader to parse a binary buffer; do not validate the magic prefix
 * until the first read.
 */
void fr_reader_init(fr_reader_t *r, const uint8_t *buf, size_t len)
{
	r->buf = buf;
	r->len = len;
	r->pos = 0;
	r->checked_magic = false;
}

/**
 * Parse one flight-recorder record from the reader buffer; check magic prefix on first call,
 * validate frame format and type; return the record type on success, 0 for clean end-of-buffer, or
 * -1 on malformed input.
 */
int fr_read_next(fr_reader_t *r, struct fr_record *out)
{
	uint8_t type;
	uint16_t plen;
	size_t o, pend;

	if (!r->checked_magic) {
		size_t mo = 0;

		if (r->len < 4u || g32(r->buf, &mo) != FR_MAGIC) {
			return -1;
		}
		r->pos = 4;
		r->checked_magic = true;
	}
	if (r->pos == r->len) {
		return 0; /* clean end */
	}
	if (r->pos + 3u > r->len) {
		return -1; /* truncated header */
	}
	o = r->pos;
	type = g8(r->buf, &o);
	plen = g16(r->buf, &o);
	pend = o + plen;
	if (pend > r->len) {
		return -1; /* truncated payload */
	}

	memset(out, 0, sizeof(*out));
	out->type = type;
	switch (type) {
	case FR_REC_META: {
		struct fr_meta *m = &out->u.meta;
		uint8_t sn;

		if (plen < 5u) {
			return -1;
		}
		m->version = g16(r->buf, &o);
		m->port = g16(r->buf, &o);
		sn = g8(r->buf, &o);
		if (sn > FR_SHA_MAX || o + sn > pend || m->version != FR_VERSION) {
			return -1;
		}
		memcpy(m->sha, r->buf + o, sn);
		m->sha[sn] = '\0';
		break;
	}
	case FR_REC_CONFIG: {
		struct fr_config *c = &out->u.config;
		uint16_t rc_len;

		if (plen < 25u + FR_URSK_LEN + 2u) {
			return -1;
		}
		c->session_id = g32(r->buf, &o);
		c->channel = g8(r->buf, &o);
		c->sync_code_index = g8(r->buf, &o);
		c->slot_duration_rstu = g16(r->buf, &o);
		c->block_duration_ms = g32(r->buf, &o);
		c->slot_per_round = g8(r->buf, &o);
		c->sts_index0 = g32(r->buf, &o);
		c->uwb_time_us = g64(r->buf, &o);
		memcpy(c->ursk, r->buf + o, FR_URSK_LEN);
		o += FR_URSK_LEN;
		rc_len = g16(r->buf, &o);
		if (rc_len > FR_RC_MAX || o + rc_len > pend) {
			return -1;
		}
		c->rc_len = rc_len;
		memcpy(c->rc, r->buf + o, rc_len);
		break;
	}
	case FR_REC_EV: {
		struct fr_ev *e = &out->u.ev;
		uint16_t fl;

		if (plen < 36u) {
			return -1;
		}
		e->ep = g8(r->buf, &o);
		e->status = g32(r->buf, &o);
		e->datalength = g16(r->buf, &o);
		e->rx_ts40 = g64(r->buf, &o);
		e->tx_ts40 = g64(r->buf, &o);
		e->systime = g32(r->buf, &o);
		e->stsq_valid = g8(r->buf, &o);
		e->stsq_val = (int16_t)g16(r->buf, &o);
		e->stsq_ret = (int32_t)g32(r->buf, &o);
		fl = g16(r->buf, &o);
		if (fl > FR_FRAME_MAX || o + fl > pend) {
			return -1;
		}
		e->frame_len = fl;
		memcpy(e->frame, r->buf + o, fl);
		break;
	}
	case FR_REC_END: {
		struct fr_end *en = &out->u.end;

		if (plen < 5u) {
			return -1;
		}
		en->n_events = g32(r->buf, &o);
		en->truncated = g8(r->buf, &o);
		break;
	}
	default:
		return -1; /* unknown record type */
	}

	r->pos = pend;
	return type;
}

/* ════════════════════════════════════════════════════════════════════════
 * On-device capture path (CONFIG_WOZ_FLIGHT_RECORDER).
 * ════════════════════════════════════════════════════════════════════════ */
#if defined(CONFIG_WOZ_FLIGHT_RECORDER)

#include <stdio.h> /* snprintf for the hex dump */

#include <deca_device_api.h>

#include <ultrawidelock/uwb.h> /* struct ultrawidelock_uwb_aliro_cfg */
#include "woz_log.h"        /* woz_printf */

#ifndef CONFIG_WOZ_FLIGHT_RECORDER_BYTES
#define CONFIG_WOZ_FLIGHT_RECORDER_BYTES 16384
#endif
#ifndef WOZ_GIT_SHA
#define WOZ_GIT_SHA "dev"
#endif

#if defined(ESP_PLATFORM)
#define FR_THIS_PORT FR_PORT_ESP32
#elif defined(__ZEPHYR__)
#define FR_THIS_PORT FR_PORT_NRF
#else
#define FR_THIS_PORT FR_PORT_HOST
#endif

/* 32 raw bytes -> 64 hex chars per `[FREC]` line. */
#define FR_DUMP_LINE 32u

static uint8_t s_ring[CONFIG_WOZ_FLIGHT_RECORDER_BYTES];
static fr_writer_t s_w;
static bool s_armed; /* capturing (fr on) */
static bool s_open;  /* a CONFIG record has been written this recording */
static bool s_ended; /* END appended; buffer is finalised */
static uint32_t s_nev;
static bool s_trunc;

/* Dump sink, so a host test can capture the serial lines instead of printing
 * them; NULL routes to woz_printf (the real UART/console). */
static void (*s_sink)(const char *line);

/**
 * Decode a 5-byte little-endian unsigned integer.
 */
static uint64_t fr_ts5(const uint8_t t[5])
{
	uint64_t v = 0;

	for (int i = 0; i < 5; i++) {
		v |= (uint64_t)t[i] << (8 * i);
	}
	return v;
}

void fr_set_enabled(bool on)
{
	if (on) {
		/* Fresh recording: reset the ring and stamp META. CONFIG lands when
		 * the next session opens (arm BEFORE the walk-up, like `lab on`). */
		fr_writer_init(&s_w, s_ring, sizeof(s_ring));
		fr_write_meta(&s_w, (uint16_t)FR_THIS_PORT, WOZ_GIT_SHA);
		s_open = false;
		s_ended = false;
		s_nev = 0;
		s_trunc = false;
		s_armed = true;
	} else {
		s_armed = false; /* keep the buffer for a later `fr dump` */
	}
}

bool fr_enabled(void)
{
	return s_armed;
}

/**
 * Set an optional callback function to receive each line of flight-recorder dump output; if NULL,
 * output goes to stdout.
 */
void fr_set_dump_sink(void (*sink)(const char *line))
{
	s_sink = sink;
}

void fr_capture_config(const struct ultrawidelock_uwb_aliro_cfg *c)
{
	struct fr_config fc;

	if (!s_armed || s_open || c == NULL) {
		return;
	}
	memset(&fc, 0, sizeof(fc));
	fc.session_id = c->session_id;
	fc.channel = c->channel;
	fc.sync_code_index = c->sync_code_index;
	fc.slot_duration_rstu = c->slot_duration_rstu;
	fc.block_duration_ms = c->block_duration_ms;
	fc.slot_per_round = c->slot_per_round;
	fc.sts_index0 = c->sts_index0;
	fc.uwb_time_us = c->uwb_time_us;
	if (c->ursk != NULL) {
		memcpy(fc.ursk, c->ursk, FR_URSK_LEN);
	}
	if (c->ranging_config != NULL && c->rc_len > 0u) {
		fc.rc_len = (uint16_t)(c->rc_len > FR_RC_MAX ? FR_RC_MAX : c->rc_len);
		memcpy(fc.rc, c->ranging_config, fc.rc_len);
	}
	if (fr_write_config(&s_w, &fc) == 0) {
		s_open = true;
	} else {
		s_trunc = true;
	}
}

/* Snapshot the DW3000 registers this entry point will read, then append the
 * event. Reads are side-effect-free, but they do cost SPI while armed — capture
 * is opt-in and perturbs walk-up timing exactly like the `lab`/`uwbdiag` traces
 * do, so arm it only for a capture run. */
void fr_capture_ev(uint8_t ep, uint32_t status, uint16_t datalength)
{
	struct fr_ev e;
	uint8_t ts[5];
	int16_t q = 0;
	uint16_t fl;

	if (!s_armed || !s_open) {
		return;
	}
	memset(&e, 0, sizeof(e));
	e.ep = ep;
	e.status = status;
	e.datalength = datalength;

	fl = datalength > FR_FRAME_MAX ? FR_FRAME_MAX : datalength;
	if (fl > 0u) {
		dwt_readrxdata(e.frame, fl, 0);
	}
	e.frame_len = fl;

	memset(ts, 0, sizeof(ts));
	dwt_readrxtimestamp_ipatov(ts);
	e.rx_ts40 = fr_ts5(ts);
	memset(ts, 0, sizeof(ts));
	dwt_readtxtimestamp(ts);
	e.tx_ts40 = fr_ts5(ts);
	e.systime = dwt_readsystimestamphi32();
	e.stsq_ret = dwt_readstsquality(&q, 0);
	e.stsq_val = q;
	e.stsq_valid = 1u;

	if (fr_write_ev(&s_w, &e) == 0) {
		s_nev++;
	} else {
		s_trunc = true;
	}
}

/* Append the END record once, then hand back the finalised buffer. */
size_t fr_finalize(const uint8_t **buf)
{
	if (!s_ended) {
		fr_write_end(&s_w, s_nev, s_trunc || s_w.overflow);
		s_ended = true;
	}
	if (buf != NULL) {
		*buf = s_w.buf;
	}
	return s_w.len;
}

/**
 * Emit a line to the registered dump sink if set, otherwise print to stdout.
 */
static void fr_emit_line(const char *line)
{
	if (s_sink != NULL) {
		s_sink(line);
	} else {
		woz_printf("%s\n", line);
	}
}

void fr_dump(void)
{
	static const char hexd[] = "0123456789abcdef";
	const uint8_t *b = NULL;
	size_t n, i;
	char line[8 + FR_DUMP_LINE * 2u + 1u];

	n = fr_finalize(&b);
	(void)snprintf(line, sizeof(line), "[FREC] begin bytes=%u", (unsigned)n);
	fr_emit_line(line);
	for (i = 0; i < n; i += FR_DUMP_LINE) {
		size_t k = (n - i < FR_DUMP_LINE) ? (n - i) : FR_DUMP_LINE;
		size_t j;

		memcpy(line, "[FREC] ", 7);
		for (j = 0; j < k; j++) {
			line[7 + j * 2] = hexd[b[i + j] >> 4];
			line[7 + j * 2 + 1] = hexd[b[i + j] & 0x0Fu];
		}
		line[7 + k * 2] = '\0';
		fr_emit_line(line);
	}
	fr_emit_line("[FREC] end");
}

void fr_clear(void)
{
	s_armed = false;
	s_open = false;
	s_ended = false;
	s_nev = 0;
	s_trunc = false;
	fr_writer_init(&s_w, s_ring, sizeof(s_ring));
	s_w.len = 0; /* drop even the magic: an un-armed dump reports empty */
}

#endif /* CONFIG_WOZ_FLIGHT_RECORDER */
