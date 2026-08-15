/* SPDX-License-Identifier: ISC */

/**
 * @file flight_recorder.h
 * Capture and replay UWB frames and session configuration from a walk-up to a host for analysis and
 * replay. Records endpoint identity, status registers, frame data, and timing metadata into a
 * fixed-size ring buffer; provides reader and writer interfaces for host tools.
 */
/*
 * flight_recorder — record a live UWB walk-up's *inputs* (session config + every
 * ccc_shim_rx.c call with its DW3000 register snapshot, in dispatch order) and
 * replay them deterministically on the host. Outputs are a pure function of
 * those inputs -- no RNG on the responder path, every clock read is a captured
 * timestamp -- so a field failure becomes a regression test at a specific event.
 * The trace is a flat little-endian stream (fr_write_* / fr_read_next) over a
 * caller-owned buffer, off the device as `[FREC]` hex lines -> .frc; the replay
 * engine is host-side (tests/host/fr_replay.c). Compiles unchanged in firmware
 * and host builds.
 */
#ifndef ULTRAWIDELOCK_FLIGHT_RECORDER_H
#define ULTRAWIDELOCK_FLIGHT_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Format identity. Bump FR_VERSION on any incompatible record-layout change;
 * fr_read_next() rejects a mismatch so a stale trace fails loud, not silent. */
#define FR_MAGIC   0x31435246u /* "FRC1" little-endian */
#define FR_VERSION 1u

/* Fixed caps sized to the CCC responder: a frame fits ultrawidelock_host_rx.rxdata[128],
 * the URSK is 32 B, a RangingConfiguration is ~17 B, a short SHA ~12 chars. */
#define FR_FRAME_MAX 128u
#define FR_URSK_LEN  32u
#define FR_RC_MAX    64u
#define FR_SHA_MAX   16u

/* Which port produced the trace (META). Host replay always runs the generic
 * (non-ESP, non-Zephyr) code path, so an ESP capture replays through it too —
 * the frames and outcomes are identical; the ESP sync-arm is a timing-only
 * optimisation that compiles out on the host. */
enum fr_port {
	FR_PORT_HOST = 0,
	FR_PORT_NRF = 1,
	FR_PORT_ESP32 = 2,
};

/* Record types. */
enum fr_rec_type {
	FR_REC_META = 1,   /* format version + port + firmware SHA */
	FR_REC_CONFIG = 2, /* the ultrawidelock_uwb_ultrawidelock_cfg that opened the session */
	FR_REC_EV = 3,     /* one call into ccc_shim_rx.c + its register snapshot */
	FR_REC_END = 4,    /* event count + truncation flag (last record) */
};

/* The ccc_shim_rx.c entry point an event drove, in device dispatch order. */
enum fr_ep {
	FR_EP_TRY_PREPOLL = 1, /* ccc_shim_rx_try_prepoll(datalength) */
	FR_EP_RX_REARM = 2,    /* prepoll_rx_rearm(cb)  — RxOk/RxTo/RxErr */
	FR_EP_TX_DONE = 3,     /* resp_tx_done(cb)      — TXFRS */
};

/* ─ Plain-data record mirrors (no pointers; safe to memcpy/serialize) ─ */

/**
 * Metadata header for a flight recorder stream: protocol version, port identifier (target), and the
 * firmware commit SHA1.
 */
struct fr_meta {
	uint16_t version;
	uint16_t port;
	char sha[FR_SHA_MAX + 1]; /* NUL-terminated */
};

/**
 * Captured credential session configuration snapshot: channel, timing parameters, STS index, URSK,
 * and responder credentials. Populated once at the start of each walk-up and replayed to
 * reconstruct session state during offline analysis.
 */
struct fr_config {
	uint32_t session_id;
	uint8_t channel;
	uint8_t sync_code_index;
	uint16_t slot_duration_rstu;
	uint32_t block_duration_ms;
	uint8_t slot_per_round;
	uint32_t sts_index0;
	uint64_t uwb_time_us;
	uint8_t ursk[FR_URSK_LEN];
	uint16_t rc_len;
	uint8_t rc[FR_RC_MAX];
};

/**
 * Single UWB event captured during a walk-up: endpoint identity, status register, frame length,
 * Ipatov and TX timestamps, STS quality metrics, and up to FR_FRAME_MAX bytes of the received
 * frame.
 */
struct fr_ev {
	uint8_t ep;          /* enum fr_ep */
	uint32_t status;     /* dwt_cb_data_t.status (0 for TRY_PREPOLL) */
	uint16_t datalength; /* frame length the entry sees */
	uint64_t rx_ts40;    /* Ipatov RX timestamp (40-bit) */
	uint64_t tx_ts40;    /* TX timestamp (40-bit) */
	uint32_t systime;    /* dwt_readsystimestamphi32 */
	uint8_t stsq_valid;  /* 1 if stsq_ret/stsq_val captured */
	int16_t stsq_val;    /* dwt_readstsquality out-param */
	int32_t stsq_ret;    /* dwt_readstsquality return */
	uint16_t frame_len;  /* bytes captured in frame[] (<= datalength, FR_FRAME_MAX) */
	uint8_t frame[FR_FRAME_MAX];
};

/**
 * Trailer record marking the end of a flight recorder session: the count of captured events and a
 * truncation flag (1 if the ring filled and later events were dropped).
 */
struct fr_end {
	uint32_t n_events;
	uint8_t truncated; /* 1 if the ring filled and later events were dropped */
};

/**
 * One record in a flight recorder stream: a discriminated union holding either a metadata header,
 * session configuration snapshot, a captured event, or the end-of-session trailer.
 */
struct fr_record {
	uint8_t type; /* enum fr_rec_type */
	union {
		struct fr_meta meta;
		struct fr_config config;
		struct fr_ev ev;
		struct fr_end end;
	} u;
};

/* ─ Writer: append records to a fixed caller-owned buffer ─────────────────
 * Every fr_write_* returns 0 on success or -1 if the record did not fit; on the
 * first non-fit the writer latches `overflow` and all further writes are no-ops,
 * so the buffer always holds a valid record prefix (never a half-written tail).
 */
typedef struct {
	uint8_t *buf;
	size_t cap;
	size_t len;
	bool overflow;
} fr_writer_t;

void fr_writer_init(fr_writer_t *w, uint8_t *buf, size_t cap);
int fr_write_meta(fr_writer_t *w, uint16_t port, const char *sha);
int fr_write_config(fr_writer_t *w, const struct fr_config *cfg);
int fr_write_ev(fr_writer_t *w, const struct fr_ev *ev);
int fr_write_end(fr_writer_t *w, uint32_t n_events, bool truncated);

/* ─ Reader: iterate a trace buffer ────────────────────────────────────────
 * fr_read_next fills *out and returns the record type (>0), 0 at clean end, or
 * -1 on a malformed/short/oversized/version-mismatched stream. The first record
 * of a well-formed trace is FR_REC_META and must carry FR_VERSION.
 */
typedef struct {
	const uint8_t *buf;
	size_t len;
	size_t pos;
	bool checked_magic;
} fr_reader_t;

void fr_reader_init(fr_reader_t *r, const uint8_t *buf, size_t len);
int fr_read_next(fr_reader_t *r, struct fr_record *out);

/* ─ On-device capture API (CONFIG_ULTRAWIDELOCK_FLIGHT_RECORDER) ─────────────────────
 * No-op inlines when the feature is compiled out, so the capture call sites in
 * ccc_shim_rx.c / ultrawidelock_uwb_facade.c cost nothing in a hardened build. */
#if defined(ESP_PLATFORM)
#include "sdkconfig.h" /* CONFIG_ULTRAWIDELOCK_FLIGHT_RECORDER (Zephyr injects autoconf.h itself) */
#endif

struct ultrawidelock_uwb_ultrawidelock_cfg; /* forward decl; the real def is in
					     * ultrawidelock_uwb_facade.h
					     */

#if defined(CONFIG_ULTRAWIDELOCK_FLIGHT_RECORDER)

void fr_set_enabled(bool on); /* arm/disarm (OFF at boot; `fr on` before a walk-up) */
bool fr_enabled(void);
void fr_capture_config(
	const struct ultrawidelock_uwb_ultrawidelock_cfg *cfg);       /* session-start hook */
void fr_capture_ev(uint8_t ep, uint32_t status, uint16_t datalength); /* per-frame hook */
size_t fr_finalize(const uint8_t **buf); /* append END once; return trace length */
void fr_dump(void);                      /* hex-encode the ring as `[FREC]` lines */
void fr_clear(void);
void fr_set_dump_sink(void (*sink)(const char *line)); /* test hook; NULL => ultrawidelock_printf */

#else

/**
 * Stub callback that enables or disables flight recording.
 * No-op when the flight recorder is disabled.
 */
static inline void fr_set_enabled(bool on)
{
	(void)on;
}
/**
 * Returns true if the flight recorder is enabled and capturing events; false otherwise.
 * Stub when disabled.
 */
static inline bool fr_enabled(void)
{
	return false;
}
/**
 * Stub callback invoked when the flight recorder captures the credential session configuration.
 * No-op when the flight recorder is disabled.
 */
static inline void fr_capture_config(const struct ultrawidelock_uwb_ultrawidelock_cfg *cfg)
{
	(void)cfg;
}
/**
 * Stub callback invoked when the flight recorder captures a UWB event (endpoint fire, status, or
 * frame length). No-op when the flight recorder is disabled.
 */
static inline void fr_capture_ev(uint8_t ep, uint32_t status, uint16_t datalength)
{
	(void)ep;
	(void)status;
	(void)datalength;
}
/**
 * Stub callback that dumps the flight recorder ring buffer to the host interface.
 * No-op when the flight recorder is disabled.
 */
static inline void fr_dump(void)
{
}
/**
 * Stub callback that clears the flight recorder ring buffer.
 * No-op when the flight recorder is disabled.
 */
static inline void fr_clear(void)
{
}

#endif /* CONFIG_ULTRAWIDELOCK_FLIGHT_RECORDER */

#ifdef __cplusplus
}
#endif

#endif /* ULTRAWIDELOCK_FLIGHT_RECORDER_H */
