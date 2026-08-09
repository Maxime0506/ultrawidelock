/**
 * @file woz_side.c — differential-RSSI baseline, temporal filter, unlock gate.
 */

#include "woz_side.h"

#include <string.h>

#define RSSI_ABSENT INT16_MIN
#define VEL_UNKNOWN INT32_MIN

void woz_side_defaults(struct woz_side_cfg *cfg)
{
	if (cfg == NULL) {
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->rssi_outside_margin_db = 6;
	cfg->rssi_threshold_band_db = 3;
	cfg->min_pkts_per_anchor = 3;
	cfg->quorum_mask =
		(uint8_t)(WOZ_SIDE_ANCHOR_BLE_INSIDE | WOZ_SIDE_ANCHOR_BLE_OUTSIDE);
	cfg->agree_windows = 3;
	cfg->dwell_ms = 400;
	cfg->evidence_fresh_ms = 1500;
	cfg->confidence_min = 70;
	cfg->classifier_ver = 1;
	cfg->calibration_ver = 1;
}

void woz_side_filter_init(struct woz_side_filter *f, const struct woz_side_cfg *cfg)
{
	if (f == NULL) {
		return;
	}
	memset(f, 0, sizeof(*f));
	if (cfg != NULL) {
		f->cfg = *cfg;
	} else {
		woz_side_defaults(&f->cfg);
	}
	f->cand = WOZ_SIDE_LABEL_UNKNOWN;
	f->committed = WOZ_SIDE_LABEL_UNKNOWN;
	f->motion = WOZ_SIDE_MOTION_UNKNOWN;
	f->last_seq = UINT32_MAX;
}

bool woz_side_transition_ok(enum woz_side_label from, enum woz_side_label to)
{
	if (from == to || from == WOZ_SIDE_LABEL_UNKNOWN || to == WOZ_SIDE_LABEL_UNKNOWN) {
		return true;
	}
	/* Direct INSIDE <-> OUTSIDE flip without THRESHOLD is not allowed. */
	if ((from == WOZ_SIDE_LABEL_INSIDE && to == WOZ_SIDE_LABEL_OUTSIDE) ||
	    (from == WOZ_SIDE_LABEL_OUTSIDE && to == WOZ_SIDE_LABEL_INSIDE)) {
		return false;
	}
	return true;
}

static bool rssi_present(int16_t rssi, uint8_t pkts, uint8_t min_pkts)
{
	return rssi != RSSI_ABSENT && pkts >= min_pkts;
}

static uint8_t clamp_u8(int v)
{
	if (v < 0) {
		return 0;
	}
	if (v > 100) {
		return 100;
	}
	return (uint8_t)v;
}

struct woz_side_raw woz_side_classify_raw(const struct woz_side_cfg *cfg,
					 const struct woz_side_features *feat)
{
	struct woz_side_raw raw;
	struct woz_side_cfg local;
	const struct woz_side_cfg *c = cfg;
	bool have_in, have_out, have_th;
	int oi, ti, ot;
	uint8_t mask = 0;
	uint8_t conf = 0;

	memset(&raw, 0, sizeof(raw));
	raw.side = WOZ_SIDE_LABEL_UNKNOWN;
	raw.outside_minus_inside_db = 0;
	raw.threshold_minus_inside_db = 0;
	raw.outside_minus_threshold_db = 0;

	if (feat == NULL) {
		return raw;
	}
	if (c == NULL) {
		woz_side_defaults(&local);
		c = &local;
	}

	if ((feat->flags & (WOZ_SIDE_F_MULTI_PHONE | WOZ_SIDE_F_SESSION_MISMATCH |
			    WOZ_SIDE_F_REPLAY | WOZ_SIDE_F_VERSION_MISMATCH |
			    WOZ_SIDE_F_DEGRADED | WOZ_SIDE_F_EVIDENCE_STALE)) != 0) {
		return raw;
	}

	if (feat->uwb_range_mm >= 0) {
		mask |= WOZ_SIDE_ANCHOR_PRIMARY_UWB;
	}

	have_in = rssi_present(feat->ble_rssi_inside_dbm, feat->ble_pkts_inside,
			       c->min_pkts_per_anchor);
	have_out = rssi_present(feat->ble_rssi_outside_dbm, feat->ble_pkts_outside,
				c->min_pkts_per_anchor);
	have_th = rssi_present(feat->ble_rssi_threshold_dbm, feat->ble_pkts_threshold,
			       c->min_pkts_per_anchor);

	if (have_in) {
		mask |= WOZ_SIDE_ANCHOR_BLE_INSIDE;
	}
	if (have_out) {
		mask |= WOZ_SIDE_ANCHOR_BLE_OUTSIDE;
	}
	if (have_th) {
		mask |= WOZ_SIDE_ANCHOR_BLE_THRESHOLD;
	}

	/*
	 * Secondary UWB peer range is recorded in the contrib mask when present,
	 * but polarity still comes from differential BLE (or an explicitly
	 * configured fusion caller). Guessing which millimetre is "inside"
	 * from the primary lock alone is how INSIDE→OUTSIDE errors start.
	 */
	if (feat->uwb_peer_mm >= 0) {
		mask |= WOZ_SIDE_ANCHOR_UWB_SATELLITE;
	}

	if (have_in && have_out) {
		oi = (int)feat->ble_rssi_outside_dbm - (int)feat->ble_rssi_inside_dbm;
		raw.outside_minus_inside_db = (int16_t)oi;

		if (have_th) {
			ti = (int)feat->ble_rssi_threshold_dbm -
			     (int)feat->ble_rssi_inside_dbm;
			ot = (int)feat->ble_rssi_outside_dbm -
			     (int)feat->ble_rssi_threshold_dbm;
			raw.threshold_minus_inside_db = (int16_t)ti;
			raw.outside_minus_threshold_db = (int16_t)ot;

			if (oi > -c->rssi_threshold_band_db &&
			    oi < c->rssi_threshold_band_db &&
			    (ot > -c->rssi_threshold_band_db &&
			     ot < c->rssi_threshold_band_db)) {
				/* Near the lintel: both outside and inside are
				 * comparable and threshold is also close. */
				raw.side = WOZ_SIDE_LABEL_THRESHOLD;
				conf = clamp_u8(55 + (c->rssi_threshold_band_db - (oi < 0 ? -oi : oi)) * 5);
				raw.contrib_mask = mask;
				raw.confidence = conf;
				return raw;
			}
		}

		if (oi >= c->rssi_outside_margin_db) {
			/* Stronger outside than inside: OUTSIDE candidate. */
			if (raw.side == WOZ_SIDE_LABEL_INSIDE) {
				/* BLE and UWB disagree: safety UNKNOWN. */
				raw.side = WOZ_SIDE_LABEL_UNKNOWN;
				raw.confidence = 0;
				raw.contrib_mask = mask;
				return raw;
			}
			raw.side = WOZ_SIDE_LABEL_OUTSIDE;
			conf = clamp_u8(60 + (oi - c->rssi_outside_margin_db) * 4);
		} else if (oi <= -c->rssi_outside_margin_db) {
			if (raw.side == WOZ_SIDE_LABEL_OUTSIDE) {
				raw.side = WOZ_SIDE_LABEL_UNKNOWN;
				raw.confidence = 0;
				raw.contrib_mask = mask;
				return raw;
			}
			raw.side = WOZ_SIDE_LABEL_INSIDE;
			conf = clamp_u8(60 + (-oi - c->rssi_outside_margin_db) * 4);
		} else if (raw.side == WOZ_SIDE_LABEL_UNKNOWN) {
			raw.side = WOZ_SIDE_LABEL_THRESHOLD;
			conf = 40;
		}
	} else if (raw.side == WOZ_SIDE_LABEL_UNKNOWN) {
		/* Insufficient BLE quorum: never invent a side from absolute RSSI. */
		raw.contrib_mask = mask;
		raw.confidence = 0;
		return raw;
	}

	raw.contrib_mask = mask;
	raw.confidence = conf;
	return raw;
}

static enum woz_side_motion motion_from(enum woz_side_label side, int32_t vel_mm_s,
					int32_t range_mm)
{
	bool closing = (vel_mm_s != VEL_UNKNOWN && vel_mm_s < -100);
	bool far = (range_mm < 0) || (range_mm > 1500);

	switch (side) {
	case WOZ_SIDE_LABEL_OUTSIDE:
		if (far) {
			return closing ? WOZ_SIDE_MOTION_OUTSIDE_APPROACHING
				       : WOZ_SIDE_MOTION_OUTSIDE_FAR;
		}
		return closing ? WOZ_SIDE_MOTION_OUTSIDE_APPROACHING
			       : WOZ_SIDE_MOTION_OUTSIDE_NEAR;
	case WOZ_SIDE_LABEL_INSIDE:
		return far ? WOZ_SIDE_MOTION_INSIDE_FAR : WOZ_SIDE_MOTION_INSIDE_NEAR;
	case WOZ_SIDE_LABEL_THRESHOLD:
		return WOZ_SIDE_MOTION_THRESHOLD;
	default:
		return WOZ_SIDE_MOTION_UNKNOWN;
	}
}

struct woz_side_decision woz_side_filter_feed(struct woz_side_filter *f,
					      const struct woz_side_features *feat)
{
	struct woz_side_decision d;
	struct woz_side_raw raw;
	uint8_t flags = 0;
	uint8_t healthy;

	memset(&d, 0, sizeof(d));
	d.side = WOZ_SIDE_LABEL_UNKNOWN;
	d.motion = WOZ_SIDE_MOTION_UNKNOWN;

	if (f == NULL || feat == NULL) {
		d.flags = WOZ_SIDE_F_DEGRADED;
		return d;
	}

	flags = feat->flags;
	if (feat->classifier_ver != f->cfg.classifier_ver ||
	    feat->calibration_ver != f->cfg.calibration_ver) {
		flags |= WOZ_SIDE_F_VERSION_MISMATCH;
	}
	if (f->obs_session_id != 0 && feat->obs_session_id != f->obs_session_id) {
		/* New observation session: reset agreement counters. */
		f->cand_n = 0;
		f->cand = WOZ_SIDE_LABEL_UNKNOWN;
		f->last_seq = UINT32_MAX;
	}
	if (f->last_seq != UINT32_MAX && feat->seq != 0 && feat->seq <= f->last_seq) {
		flags |= WOZ_SIDE_F_REPLAY;
	}

	healthy = (uint8_t)(feat->anchor_health_mask & f->cfg.quorum_mask);
	if (healthy != f->cfg.quorum_mask) {
		flags |= WOZ_SIDE_F_QUORUM_FAIL;
	}

	raw = woz_side_classify_raw(&f->cfg, feat);
	if ((flags & (WOZ_SIDE_F_MULTI_PHONE | WOZ_SIDE_F_SESSION_MISMATCH |
		      WOZ_SIDE_F_REPLAY | WOZ_SIDE_F_VERSION_MISMATCH |
		      WOZ_SIDE_F_DEGRADED | WOZ_SIDE_F_EVIDENCE_STALE |
		      WOZ_SIDE_F_QUORUM_FAIL)) != 0) {
		raw.side = WOZ_SIDE_LABEL_UNKNOWN;
		raw.confidence = 0;
	}

	if (raw.side == WOZ_SIDE_LABEL_INSIDE &&
	    f->committed == WOZ_SIDE_LABEL_OUTSIDE &&
	    !woz_side_transition_ok(f->committed, raw.side)) {
		flags |= WOZ_SIDE_F_INSIDE_CONTRADICT;
		raw.side = WOZ_SIDE_LABEL_UNKNOWN;
		raw.confidence = 0;
	}

	f->obs_session_id = feat->obs_session_id;
	f->last_seq = feat->seq;
	f->flags = flags;
	f->contrib_mask = raw.contrib_mask;

	if (raw.side == f->cand && raw.side != WOZ_SIDE_LABEL_UNKNOWN) {
		if (f->cand_n < 255) {
			f->cand_n++;
		}
	} else {
		f->cand = raw.side;
		f->cand_n = (raw.side == WOZ_SIDE_LABEL_UNKNOWN) ? 0 : 1;
		f->cand_since_ms = feat->now_ms;
	}
	f->confidence = raw.confidence;

	if (raw.side != WOZ_SIDE_LABEL_UNKNOWN && f->cand_n >= f->cfg.agree_windows &&
	    (feat->now_ms - f->cand_since_ms) >= (int64_t)f->cfg.dwell_ms &&
	    woz_side_transition_ok(f->committed, raw.side)) {
		f->committed = raw.side;
		f->committed_ms = feat->now_ms;
		if (raw.side == WOZ_SIDE_LABEL_OUTSIDE) {
			f->last_outside_ms = feat->now_ms;
		} else if (raw.side == WOZ_SIDE_LABEL_INSIDE) {
			f->last_inside_ms = feat->now_ms;
		}
	} else if (raw.side == WOZ_SIDE_LABEL_UNKNOWN) {
		/* Confidence decay: do not keep a stale committed OUTSIDE live. */
		if (f->committed != WOZ_SIDE_LABEL_UNKNOWN &&
		    (feat->now_ms - f->committed_ms) > (int64_t)f->cfg.evidence_fresh_ms) {
			f->committed = WOZ_SIDE_LABEL_UNKNOWN;
			f->confidence = 0;
		}
	}

	/* A lone spike toward OUTSIDE while committed INSIDE never commits. */
	if (f->committed == WOZ_SIDE_LABEL_INSIDE &&
	    raw.side == WOZ_SIDE_LABEL_OUTSIDE &&
	    f->cand_n < f->cfg.agree_windows) {
		f->committed = WOZ_SIDE_LABEL_INSIDE;
	}

	f->motion = motion_from(f->committed, feat->uwb_vel_mm_s, feat->uwb_range_mm);

	d.side = f->committed;
	d.motion = f->motion;
	d.confidence = f->confidence;
	d.contrib_mask = f->contrib_mask;
	d.flags = f->flags;
	d.classifier_ver = f->cfg.classifier_ver;
	d.calibration_ver = f->cfg.calibration_ver;
	d.obs_session_id = feat->obs_session_id;
	d.seq = feat->seq;
	d.decided_ms = feat->now_ms;

	if (d.side != WOZ_SIDE_LABEL_UNKNOWN &&
	    (feat->now_ms - f->committed_ms) > (int64_t)f->cfg.evidence_fresh_ms) {
		d.side = WOZ_SIDE_LABEL_UNKNOWN;
		d.flags |= WOZ_SIDE_F_EVIDENCE_STALE;
		d.confidence = 0;
	}

	return d;
}

bool woz_side_may_passive_unlock(const struct woz_side_decision *d,
				 const struct woz_side_cfg *cfg)
{
	struct woz_side_cfg local;
	const struct woz_side_cfg *c = cfg;
	uint8_t min_conf;

	if (d == NULL) {
		return false;
	}
	if (c == NULL) {
		woz_side_defaults(&local);
		c = &local;
	}
	min_conf = c->confidence_min;

	if (d->side != WOZ_SIDE_LABEL_OUTSIDE) {
		return false;
	}
	if (d->confidence < min_conf) {
		return false;
	}
	if ((d->flags & (WOZ_SIDE_F_DEGRADED | WOZ_SIDE_F_MULTI_PHONE |
			 WOZ_SIDE_F_SESSION_MISMATCH | WOZ_SIDE_F_REPLAY |
			 WOZ_SIDE_F_VERSION_MISMATCH | WOZ_SIDE_F_INSIDE_CONTRADICT |
			 WOZ_SIDE_F_EVIDENCE_STALE | WOZ_SIDE_F_QUORUM_FAIL)) != 0) {
		return false;
	}
	return true;
}
