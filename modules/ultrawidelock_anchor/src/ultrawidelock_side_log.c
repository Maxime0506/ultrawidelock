/**
 * @file ultrawidelock_side_log.c — pack/check bounded side-decision records.
 */

#include "ultrawidelock_side_log.h"

#include <string.h>

uint16_t ultrawidelock_side_log_crc16(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint16_t crc = 0xffffu;

	if (p == NULL) {
		return 0;
	}
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)p[i] << 8;
		for (int b = 0; b < 8; b++) {
			if (crc & 0x8000u) {
				crc = (uint16_t)((crc << 1) ^ 0x1021u);
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

int ultrawidelock_side_log_pack(const struct ultrawidelock_side_features *feat,
		      const struct ultrawidelock_side_decision *dec, const struct ultrawidelock_side_raw *raw,
		      struct ultrawidelock_side_log_rec *out)
{
	if (feat == NULL || dec == NULL || out == NULL) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->magic = ULTRAWIDELOCK_SIDE_LOG_MAGIC;
	out->version = ULTRAWIDELOCK_SIDE_LOG_VERSION;
	out->side = (uint8_t)dec->side;
	out->motion = (uint8_t)dec->motion;
	out->confidence = dec->confidence;
	out->contrib_mask = dec->contrib_mask;
	out->flags = dec->flags;
	out->obs_session_id = dec->obs_session_id;
	out->seq = dec->seq;
	out->now_ms_lo = (uint32_t)feat->now_ms;
	out->uwb_range_mm = feat->uwb_range_mm;
	out->ble_in_dbm = feat->ble_rssi_inside_dbm;
	out->ble_out_dbm = feat->ble_rssi_outside_dbm;
	out->ble_th_dbm = feat->ble_rssi_threshold_dbm;
	out->oi_db = (raw != NULL) ? raw->outside_minus_inside_db : 0;
	out->classifier_ver = dec->classifier_ver;
	out->calibration_ver = dec->calibration_ver;
	out->ble_pkts_in = feat->ble_pkts_inside;
	out->ble_pkts_out = feat->ble_pkts_outside;
	out->ble_pkts_th = feat->ble_pkts_threshold;
	out->crc16 = ultrawidelock_side_log_crc16(out, offsetof(struct ultrawidelock_side_log_rec, crc16));
	return (int)sizeof(*out);
}

int ultrawidelock_side_log_check(const struct ultrawidelock_side_log_rec *rec)
{
	uint16_t crc;

	if (rec == NULL) {
		return -1;
	}
	if (rec->magic != ULTRAWIDELOCK_SIDE_LOG_MAGIC || rec->version != ULTRAWIDELOCK_SIDE_LOG_VERSION) {
		return -1;
	}
	crc = ultrawidelock_side_log_crc16(rec, offsetof(struct ultrawidelock_side_log_rec, crc16));
	return (crc == rec->crc16) ? 0 : -1;
}
