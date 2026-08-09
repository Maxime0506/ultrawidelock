/**
 * @file test_woz_side.c — fail-closed passive unlock policy and RSSI baseline.
 */

#include "test.h"

#include "woz_side.h"
#include "woz_side_log.h"

#include <string.h>

static struct woz_side_features base_feat(void)
{
	struct woz_side_features f;

	memset(&f, 0, sizeof(f));
	f.obs_session_id = 0xA11U;
	f.seq = 1;
	f.now_ms = 10000;
	f.uwb_range_mm = 800;
	f.uwb_vel_mm_s = -400;
	f.uwb_range_var_mm = 20;
	f.ble_rssi_inside_dbm = -70;
	f.ble_rssi_outside_dbm = -55;
	f.ble_rssi_threshold_dbm = -60;
	f.ble_pkts_inside = 8;
	f.ble_pkts_outside = 8;
	f.ble_pkts_threshold = 8;
	f.uwb_peer_mm = -1;
	f.classifier_ver = 1;
	f.calibration_ver = 1;
	f.anchor_health_mask =
		(uint8_t)(WOZ_SIDE_ANCHOR_BLE_INSIDE | WOZ_SIDE_ANCHOR_BLE_OUTSIDE |
			  WOZ_SIDE_ANCHOR_PRIMARY_UWB);
	return f;
}

static struct woz_side_decision feed_n(struct woz_side_filter *filt, struct woz_side_features *f,
				       int n)
{
	struct woz_side_decision d = {0};

	for (int i = 0; i < n; i++) {
		f->seq = (uint32_t)(i + 1);
		f->now_ms = 10000 + (int64_t)i * 200;
		d = woz_side_filter_feed(filt, f);
	}
	return d;
}

static void test_raw_differential(void)
{
	struct woz_side_cfg cfg;
	struct woz_side_features f = base_feat();
	struct woz_side_raw raw;

	t_group("side: differential RSSI baseline");
	woz_side_defaults(&cfg);

	raw = woz_side_classify_raw(&cfg, &f);
	T_EQ("raw.outside", raw.side, WOZ_SIDE_LABEL_OUTSIDE);
	T_OK("raw.oi", raw.outside_minus_inside_db == 15);
	T_OK("raw.conf", raw.confidence >= cfg.confidence_min);

	f.ble_rssi_inside_dbm = -50;
	f.ble_rssi_outside_dbm = -70;
	raw = woz_side_classify_raw(&cfg, &f);
	T_EQ("raw.inside", raw.side, WOZ_SIDE_LABEL_INSIDE);

	f.ble_rssi_inside_dbm = -60;
	f.ble_rssi_outside_dbm = -58;
	raw = woz_side_classify_raw(&cfg, &f);
	T_EQ("raw.threshold_band", raw.side, WOZ_SIDE_LABEL_THRESHOLD);

	f.ble_pkts_outside = 1;
	raw = woz_side_classify_raw(&cfg, &f);
	T_EQ("raw.no_quorum_pkts", raw.side, WOZ_SIDE_LABEL_UNKNOWN);

	f = base_feat();
	f.ble_rssi_outside_dbm = INT16_MIN;
	f.ble_pkts_outside = 0;
	raw = woz_side_classify_raw(&cfg, &f);
	T_EQ("raw.missing_outside", raw.side, WOZ_SIDE_LABEL_UNKNOWN);
}

static void test_policy_fail_closed(void)
{
	struct woz_side_cfg cfg;
	struct woz_side_decision d;

	t_group("side: passive unlock is fail-closed");
	woz_side_defaults(&cfg);
	memset(&d, 0, sizeof(d));

	T_OK("null_dec", !woz_side_may_passive_unlock(NULL, &cfg));

	d.side = WOZ_SIDE_LABEL_OUTSIDE;
	d.confidence = 90;
	T_OK("outside_ok", woz_side_may_passive_unlock(&d, &cfg));

	d.side = WOZ_SIDE_LABEL_INSIDE;
	T_OK("inside_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.side = WOZ_SIDE_LABEL_THRESHOLD;
	T_OK("threshold_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.side = WOZ_SIDE_LABEL_UNKNOWN;
	T_OK("unknown_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.side = WOZ_SIDE_LABEL_OUTSIDE;
	d.confidence = 10;
	T_OK("low_conf_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.confidence = 90;
	d.flags = WOZ_SIDE_F_QUORUM_FAIL;
	T_OK("quorum_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.flags = WOZ_SIDE_F_MULTI_PHONE;
	T_OK("multi_phone_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.flags = WOZ_SIDE_F_EVIDENCE_STALE;
	T_OK("stale_blocked", !woz_side_may_passive_unlock(&d, &cfg));

	d.flags = WOZ_SIDE_F_DEGRADED;
	T_OK("degraded_blocked", !woz_side_may_passive_unlock(&d, &cfg));
}

static void test_temporal_no_spike_flip(void)
{
	struct woz_side_filter filt;
	struct woz_side_cfg cfg;
	struct woz_side_features f = base_feat();
	struct woz_side_decision d;

	t_group("side: single spike cannot flip INSIDE to OUTSIDE");
	woz_side_defaults(&cfg);
	cfg.agree_windows = 3;
	cfg.dwell_ms = 200;
	woz_side_filter_init(&filt, &cfg);

	/* Establish INSIDE. */
	f.ble_rssi_inside_dbm = -50;
	f.ble_rssi_outside_dbm = -70;
	d = feed_n(&filt, &f, 5);
	T_EQ("commit.inside", d.side, WOZ_SIDE_LABEL_INSIDE);
	T_OK("inside.no_unlock", !woz_side_may_passive_unlock(&d, &cfg));

	/* One outside-looking spike, still within the evidence window. */
	f.ble_rssi_inside_dbm = -70;
	f.ble_rssi_outside_dbm = -50;
	f.seq = 10;
	f.now_ms = 11200;
	d = woz_side_filter_feed(&filt, &f);
	T_OK("spike.not_outside", d.side != WOZ_SIDE_LABEL_OUTSIDE);
	T_EQ("spike.still_inside", d.side, WOZ_SIDE_LABEL_INSIDE);
	T_OK("spike.no_unlock", !woz_side_may_passive_unlock(&d, &cfg));
}

static void test_outside_approach_unlock(void)
{
	struct woz_side_filter filt;
	struct woz_side_cfg cfg;
	struct woz_side_features f = base_feat();
	struct woz_side_decision d;

	t_group("side: confident OUTSIDE may passively unlock");
	woz_side_defaults(&cfg);
	cfg.agree_windows = 3;
	cfg.dwell_ms = 200;
	woz_side_filter_init(&filt, &cfg);

	d = feed_n(&filt, &f, 5);
	T_EQ("commit.outside", d.side, WOZ_SIDE_LABEL_OUTSIDE);
	T_OK("outside.unlock", woz_side_may_passive_unlock(&d, &cfg));
}

static void test_transition_rules(void)
{
	t_group("side: transition plausibility");
	T_OK("same_ok", woz_side_transition_ok(WOZ_SIDE_LABEL_OUTSIDE, WOZ_SIDE_LABEL_OUTSIDE));
	T_OK("via_threshold",
	     woz_side_transition_ok(WOZ_SIDE_LABEL_OUTSIDE, WOZ_SIDE_LABEL_THRESHOLD));
	T_OK("direct_flip_blocked",
	     !woz_side_transition_ok(WOZ_SIDE_LABEL_INSIDE, WOZ_SIDE_LABEL_OUTSIDE));
	T_OK("unknown_ok",
	     woz_side_transition_ok(WOZ_SIDE_LABEL_UNKNOWN, WOZ_SIDE_LABEL_OUTSIDE));
}

static void test_log_roundtrip(void)
{
	struct woz_side_features f = base_feat();
	struct woz_side_decision d;
	struct woz_side_raw raw;
	struct woz_side_log_rec rec;
	struct woz_side_cfg cfg;

	t_group("side: binary log pack/check");
	woz_side_defaults(&cfg);
	raw = woz_side_classify_raw(&cfg, &f);
	memset(&d, 0, sizeof(d));
	d.side = raw.side;
	d.confidence = raw.confidence;
	d.contrib_mask = raw.contrib_mask;
	d.obs_session_id = f.obs_session_id;
	d.seq = f.seq;
	d.classifier_ver = 1;
	d.calibration_ver = 1;

	T_EQ("pack", woz_side_log_pack(&f, &d, &raw, &rec), (long)WOZ_SIDE_LOG_SIZE);
	T_EQ("check", woz_side_log_check(&rec), 0);
	rec.confidence ^= 0xff;
	T_OK("bad_crc", woz_side_log_check(&rec) != 0);
}

void test_woz_side(void)
{
	test_raw_differential();
	test_policy_fail_closed();
	test_temporal_no_spike_flip();
	test_outside_approach_unlock();
	test_transition_rules();
	test_log_roundtrip();
}
