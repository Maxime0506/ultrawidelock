/**
 * @file test_woz_side_replay.c — HITL-style replay of labelled differential windows.
 */

#include "test.h"

#include "woz_side.h"

#include <string.h>

static struct woz_side_decision play(struct woz_side_filter *filt, struct woz_side_features *f,
				     int16_t in_dbm, int16_t out_dbm, int16_t th_dbm, int i)
{
	f->seq = (uint32_t)(i + 1);
	f->now_ms = 1000 + (int64_t)i * 200;
	f->uwb_range_mm = 2000 - i * 150;
	f->ble_rssi_inside_dbm = in_dbm;
	f->ble_rssi_outside_dbm = out_dbm;
	f->ble_rssi_threshold_dbm = th_dbm;
	return woz_side_filter_feed(filt, f);
}

static void prep(struct woz_side_filter *filt, struct woz_side_cfg *cfg,
		 struct woz_side_features *f)
{
	woz_side_defaults(cfg);
	cfg->agree_windows = 3;
	cfg->dwell_ms = 200;
	woz_side_filter_init(filt, cfg);
	memset(f, 0, sizeof(*f));
	f->obs_session_id = 7;
	f->uwb_vel_mm_s = -500;
	f->uwb_range_var_mm = 30;
	f->ble_pkts_inside = 8;
	f->ble_pkts_outside = 8;
	f->ble_pkts_threshold = 8;
	f->uwb_peer_mm = -1;
	f->classifier_ver = 1;
	f->calibration_ver = 1;
	f->anchor_health_mask =
		(uint8_t)(WOZ_SIDE_ANCHOR_BLE_INSIDE | WOZ_SIDE_ANCHOR_BLE_OUTSIDE |
			  WOZ_SIDE_ANCHOR_BLE_THRESHOLD | WOZ_SIDE_ANCHOR_PRIMARY_UWB);
}

void test_woz_side_replay(void)
{
	struct woz_side_filter filt;
	struct woz_side_cfg cfg;
	struct woz_side_features f;
	struct woz_side_decision d;
	int unlocks = 0;

	t_group("side replay: outside approach may unlock");
	prep(&filt, &cfg, &f);
	d = play(&filt, &f, -72, -55, -60, 0);
	T_OK("out.early0", !woz_side_may_passive_unlock(&d, &cfg));
	d = play(&filt, &f, -71, -54, -59, 1);
	T_OK("out.early1", !woz_side_may_passive_unlock(&d, &cfg));
	d = play(&filt, &f, -70, -53, -58, 2);
	T_OK("out.commit", woz_side_may_passive_unlock(&d, &cfg));
	T_EQ("out.side", d.side, WOZ_SIDE_LABEL_OUTSIDE);
	d = play(&filt, &f, -65, -50, -55, 3);
	T_OK("out.hold", woz_side_may_passive_unlock(&d, &cfg));

	t_group("side replay: inside near door never unlocks");
	prep(&filt, &cfg, &f);
	unlocks = 0;
	d = play(&filt, &f, -52, -70, -60, 0);
	unlocks += woz_side_may_passive_unlock(&d, &cfg);
	d = play(&filt, &f, -51, -71, -61, 1);
	unlocks += woz_side_may_passive_unlock(&d, &cfg);
	d = play(&filt, &f, -50, -72, -62, 2);
	unlocks += woz_side_may_passive_unlock(&d, &cfg);
	d = play(&filt, &f, -50, -70, -60, 3);
	unlocks += woz_side_may_passive_unlock(&d, &cfg);
	d = play(&filt, &f, -49, -69, -59, 4);
	unlocks += woz_side_may_passive_unlock(&d, &cfg);
	T_EQ("in.no_unlocks", unlocks, 0);
	T_OK("in.not_outside", d.side != WOZ_SIDE_LABEL_OUTSIDE);
}
