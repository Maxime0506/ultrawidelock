/**
 * @file woz_side_log.h — bounded binary record for side features and decisions.
 *
 * Fixed 48-byte little-endian record. No credentials, no UWB session keys, no
 * stable phone identifiers. Bound to ephemeral obs_session_id + seq.
 */

#ifndef WOZ_SIDE_LOG_H
#define WOZ_SIDE_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "woz_side.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WOZ_SIDE_LOG_MAGIC   0x5344u /* 'SD' */
#define WOZ_SIDE_LOG_VERSION 1u
#define WOZ_SIDE_LOG_SIZE    48u

/** Packed on-wire / on-UART record. */
struct woz_side_log_rec {
	uint16_t magic;
	uint8_t version;
	uint8_t side;
	uint8_t motion;
	uint8_t confidence;
	uint8_t contrib_mask;
	uint8_t flags;
	uint32_t obs_session_id;
	uint32_t seq;
	uint32_t now_ms_lo;
	int32_t uwb_range_mm;
	int16_t ble_in_dbm;
	int16_t ble_out_dbm;
	int16_t ble_th_dbm;
	int16_t oi_db;
	uint8_t classifier_ver;
	uint8_t calibration_ver;
	uint8_t ble_pkts_in;
	uint8_t ble_pkts_out;
	uint8_t ble_pkts_th;
	uint8_t reserved0;
	uint16_t crc16;
	uint8_t pad[8]; /**< reserved; keeps the record a round 48 bytes */
};

_Static_assert(sizeof(struct woz_side_log_rec) == WOZ_SIDE_LOG_SIZE,
	       "woz_side_log_rec size");

/** Pack features + decision into @p out. Returns bytes written (48) or -1. */
int woz_side_log_pack(const struct woz_side_features *feat,
		      const struct woz_side_decision *dec, const struct woz_side_raw *raw,
		      struct woz_side_log_rec *out);

/** Validate magic/version/CRC. Returns 0 on success, -1 on reject. */
int woz_side_log_check(const struct woz_side_log_rec *rec);

uint16_t woz_side_log_crc16(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WOZ_SIDE_LOG_H */
