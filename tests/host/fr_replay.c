/*
 * fr_replay — see fr_replay.h. Drives the real listener through the DW3000 shim.
 */
#include "fr_replay.h"

#include <string.h>

#include <deca_device_api.h> /* woz_host_rx, dwt_cb_data_t, dwt_cb_t */

#include "ccc_shim.h"        /* ccc_shim_rx_try_prepoll */
#include "fira_session.h"    /* fira_session_last_range */
#include <ultrawidelock/uwb.h>  /* ultrawidelock_uwb_start_aliro / ultrawidelock_uwb_stop */

/* Load the DW3000 register snapshot this event carries into the shim doubles, so
 * the entry point reads exactly what the device saw. */
static void load_regs(const struct fr_ev *e)
{
	uint16_t n = e->frame_len;

	if (n > sizeof(woz_host_rx.rxdata)) {
		n = (uint16_t)sizeof(woz_host_rx.rxdata);
	}
	memset(woz_host_rx.rxdata, 0, sizeof(woz_host_rx.rxdata));
	memcpy(woz_host_rx.rxdata, e->frame, n);
	woz_host_rx.rxdata_len = (e->datalength > sizeof(woz_host_rx.rxdata))
					 ? (uint16_t)sizeof(woz_host_rx.rxdata)
					 : e->datalength;
	woz_host_rx.rx_ts40 = e->rx_ts40;
	woz_host_rx.tx_ts40 = e->tx_ts40;
	woz_host_rx.systime = e->systime;
	if (e->stsq_valid) {
		woz_host_rx.stsq_ret = e->stsq_ret;
		woz_host_rx.stsq_val = e->stsq_val;
	}
}

/* Dispatch one event to the same ccc_shim_rx.c entry the device drove. */
static void dispatch(const struct fr_ev *e)
{
	dwt_cb_data_t d;

	memset(&d, 0, sizeof(d));
	d.status = e->status;
	d.datalength = e->datalength;

	switch (e->ep) {
	case FR_EP_TRY_PREPOLL:
		ccc_shim_rx_try_prepoll(e->datalength);
		break;
	case FR_EP_RX_REARM:
		if (woz_host_rx.cbs.cbRxOk != NULL) {
			woz_host_rx.cbs.cbRxOk(&d);
		}
		break;
	case FR_EP_TX_DONE:
		if (woz_host_rx.cbs.cbTxDone != NULL) {
			woz_host_rx.cbs.cbTxDone(&d);
		}
		break;
	default:
		break; /* unknown ep: no-op (reader already validated known types) */
	}
}

static void snapshot(struct fr_output *o, uint8_t ep)
{
	o->ep = ep;
	o->rxenable_calls = woz_host_rx.rxenable_calls;
	o->last_rxenable_mode = woz_host_rx.last_rxenable_mode;
	o->starttx_calls = woz_host_rx.starttx_calls;
	o->forcetrxoff_calls = woz_host_rx.forcetrxoff_calls;
}

bool fr_replay_run(const uint8_t *trace, size_t len, struct fr_replay_result *out)
{
	fr_reader_t r;
	struct fr_record rec;         /* current record */
	struct fr_config cfg_store;   /* kept alive for the whole session */
	struct ultrawidelock_uwb_aliro_cfg cfg;
	bool have_config = false;
	bool saw_end = false;
	int idx = 0;
	int type;

	memset(out, 0, sizeof(*out));
	out->err_at = -1;
	fr_reader_init(&r, trace, len);

	/* First record must be META with our version. */
	type = fr_read_next(&r, &rec);
	if (type != FR_REC_META) {
		out->err_at = idx;
		return false;
	}
	out->port = rec.u.meta.port;
	memcpy(out->sha, rec.u.meta.sha, sizeof(out->sha));
	idx++;

	while ((type = fr_read_next(&r, &rec)) > 0) {
		if (type == FR_REC_CONFIG) {
			if (have_config) {
				out->err_at = idx; /* one session per trace (v1) */
				return false;
			}
			cfg_store = rec.u.config;
			memset(&cfg, 0, sizeof(cfg));
			cfg.session_id = cfg_store.session_id;
			cfg.channel = cfg_store.channel;
			cfg.sync_code_index = cfg_store.sync_code_index;
			cfg.slot_duration_rstu = cfg_store.slot_duration_rstu;
			cfg.block_duration_ms = cfg_store.block_duration_ms;
			cfg.slot_per_round = cfg_store.slot_per_round;
			cfg.sts_index0 = cfg_store.sts_index0;
			cfg.uwb_time_us = cfg_store.uwb_time_us;
			cfg.ursk = cfg_store.ursk;
			cfg.ranging_config = cfg_store.rc_len ? cfg_store.rc : NULL;
			cfg.rc_len = cfg_store.rc_len;

			woz_host_rx_reset();
			if (ultrawidelock_uwb_start_aliro(&cfg) != 0) {
				out->err_at = idx;
				return false;
			}
			have_config = true;
		} else if (type == FR_REC_EV) {
			if (!have_config || out->n_events >= FR_REPLAY_MAX_EV) {
				out->err_at = idx;
				return false;
			}
			load_regs(&rec.u.ev);
			dispatch(&rec.u.ev);
			snapshot(&out->out[out->n_events], rec.u.ev.ep);
			out->n_events++;
		} else if (type == FR_REC_END) {
			out->end_n_events = rec.u.end.n_events;
			out->truncated = rec.u.end.truncated != 0;
			saw_end = true;
			break;
		}
		idx++;
	}

	if (type < 0) {
		out->err_at = idx;
		return false;
	}

	if (have_config) {
		int32_t cm = 0;

		out->range_valid = fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
		out->range_cm = out->range_valid ? cm : 0;
		ultrawidelock_uwb_stop(); /* leave the radio quiet for the next suite */
	}

	out->ok = saw_end;
	return saw_end;
}
