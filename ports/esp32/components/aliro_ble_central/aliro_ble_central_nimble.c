// NimBLE central/client backend for the Aliro initiator: the mirror of
// components/aliro_ble/aliro_ble.c. That file advertises 0xFFF2, serves the
// characteristics and runs a CoC server; this one scans for 0xFFF2, connects,
// discovers, reads the reader's SPSM/versions, writes the selected version and
// opens a CoC client to that SPSM.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * ESP32-only backend behind the shared aliro_ble_central.h. The decoding it
 * feeds on (advert, READ payload, BleSK salt) is platform-free and lives in
 * modules/woz_aliro/src/aliro_ble_central.c, host-tested separately; everything
 * here is stack plumbing that only silicon can exercise.
 *
 * Bring-up is one linear chain, each step resumed from the previous callback:
 *   sync -> scan -> match advert -> connect -> discover service ->
 *   discover characteristics -> READ spsm/versions -> WRITE our version ->
 *   L2CAP CoC connect -> on_ready
 * Any failure logs and returns to scanning, so a reader that reboots mid-chain
 * is picked up again without intervention.
 */
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_l2cap.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "aliro_ble_central.h"

static const char *TAG = "aliro_central";

/* Aliro service, 16-bit 0xFFF2 — the one the reader advertises and serves. */
static const ble_uuid16_t k_svc_uuid = BLE_UUID16_INIT(0xFFF2u);

/* Reader SPSM + supported protocol versions (READ), and the user-device selected
 * version (WRITE). Byte order mirrors aliro_ble.c: NimBLE stores 128-bit UUIDs
 * little-endian, so these are the reversed forms of
 * D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3 and BD4B9502-3F54-11EC-B919-0242AC120005. */
static const ble_uuid128_t k_chr_reader_spsm_uuid =
	BLE_UUID128_INIT(0xa3, 0x80, 0xf9, 0xe5, 0x1e, 0x6b, 0xe4, 0x8b, 0x3a, 0x4b, 0x23, 0x9e,
			 0x30, 0xa1, 0xb5, 0xd3);
static const ble_uuid128_t k_chr_device_ver_uuid =
	BLE_UUID128_INIT(0x05, 0x00, 0x12, 0xac, 0x42, 0x02, 0x19, 0xb9, 0xec, 0x11, 0x54, 0x3f,
			 0x02, 0x95, 0x4b, 0xbd);

/* Same CoC sizing as the reader side so an SDU that fits one fits the other. */
#ifndef CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM
#define CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM 1
#endif
#define ALIRO_L2CAP_MTU     512u
#define ALIRO_COC_BUF_COUNT (6u * CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM)

static os_membuf_t s_coc_mem[OS_MEMPOOL_SIZE(ALIRO_COC_BUF_COUNT, ALIRO_L2CAP_MTU)];
static struct os_mempool s_coc_mempool;
static struct os_mbuf_pool s_coc_mbuf_pool;

static struct aliro_ble_central_config s_cfg;
static uint8_t s_own_addr_type;

/* Set the moment we decide to connect, cleared when we go back to scanning.
 * Suppresses the DISC reports NimBLE has already queued behind the cancel. */
static bool s_connecting;

/* The one peer we are talking to. conn_handle is BLE_HS_CONN_HANDLE_NONE when
 * idle; the rest is filled in as the discovery chain advances. */
static struct {
	uint16_t conn_handle;
	uint16_t svc_start;
	uint16_t svc_end;
	uint16_t spsm_val_handle;
	uint16_t devver_val_handle;
	struct ble_l2cap_chan *chan;
	struct aliro_ble_central_peer peer;
} s_peer;

static void start_scan(void);

/**
 * Clear the peer state structure and reset the connection handle to BLE_HS_CONN_HANDLE_NONE.
 */
static void reset_peer(void)
{
	memset(&s_peer, 0, sizeof(s_peer));
	s_peer.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

/* Abandon this peer and go back to scanning. Called from every failure path so
 * a half-finished chain can never leave the app wedged. */
static void abandon(const char *why, int rc)
{
	ESP_LOGW(TAG, "%s (rc=%d); rescanning", why, rc);
	if (s_peer.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
		ble_gap_terminate(s_peer.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
	}
	reset_peer();
	start_scan();
}

/* ---- L2CAP CoC client ---- */

/**
 * Allocate and arm an RX buffer on the L2CAP CoC channel. Return BLE_HS_ENOMEM if no buffers are
 * available, otherwise return the ble_l2cap_recv_ready result.
 */
static int coc_arm_rx(struct ble_l2cap_chan *chan)
{
	struct os_mbuf *rx = os_mbuf_get_pkthdr(&s_coc_mbuf_pool, 0);

	if (rx == NULL) {
		ESP_LOGE(TAG, "coc: out of rx buffers");
		return BLE_HS_ENOMEM;
	}
	return ble_l2cap_recv_ready(chan, rx);
}

static int l2cap_event_cb(struct ble_l2cap_event *event, void *arg)
{
	(void)arg;
	switch (event->type) {
	case BLE_L2CAP_EVENT_COC_CONNECTED:
		if (event->connect.status != 0) {
			abandon("coc connect failed", event->connect.status);
			return 0;
		}
		s_peer.chan = event->connect.chan;
		ESP_LOGI(TAG, "coc connected (conn %u, SPSM 0x%04x)", event->connect.conn_handle,
			 (unsigned)s_peer.peer.spsm);
		if (s_cfg.cb.on_ready) {
			s_cfg.cb.on_ready(event->connect.conn_handle, &s_peer.peer);
		}
		return 0;

	case BLE_L2CAP_EVENT_COC_DISCONNECTED:
		ESP_LOGI(TAG, "coc disconnected (conn %u)", event->disconnect.conn_handle);
		s_peer.chan = NULL;
		if (s_cfg.cb.on_closed) {
			s_cfg.cb.on_closed(event->disconnect.conn_handle);
		}
		return 0;

	case BLE_L2CAP_EVENT_COC_DATA_RECEIVED: {
		struct os_mbuf *om = event->receive.sdu_rx;

		if (om != NULL) {
			uint8_t buf[ALIRO_L2CAP_MTU];
			uint16_t len = 0;

			if (ble_hs_mbuf_to_flat(om, buf, sizeof(buf), &len) == 0 &&
			    s_cfg.cb.on_data) {
				s_cfg.cb.on_data(event->receive.conn_handle, buf, len);
			}
			os_mbuf_free_chain(om);
		}
		return coc_arm_rx(event->receive.chan);
	}

	default:
		return 0;
	}
}

/* Final step of the chain: open the CoC to the SPSM the READ gave us. */
static void coc_connect(void)
{
	struct os_mbuf *sdu_rx = os_mbuf_get_pkthdr(&s_coc_mbuf_pool, 0);

	if (sdu_rx == NULL) {
		abandon("coc: no rx buffer for connect", 0);
		return;
	}

	int rc = ble_l2cap_connect(s_peer.conn_handle, s_peer.peer.spsm, ALIRO_L2CAP_MTU, sdu_rx,
				   l2cap_event_cb, NULL);

	if (rc != 0) {
		os_mbuf_free_chain(sdu_rx);
		abandon("ble_l2cap_connect", rc);
	}
}

/* ---- GATT discovery chain ---- */

/**
 * Callback when the device-version characteristic write completes. On success, open the L2CAP CoC
 * channel to the SPSM previously read. On error, abandon this peer.
 */
static int on_devver_write(uint16_t conn_handle, const struct ble_gatt_error *error,
			   struct ble_gatt_attr *attr, void *arg)
{
	(void)conn_handle;
	(void)attr;
	(void)arg;
	if (error->status != 0) {
		abandon("device-version write", error->status);
		return 0;
	}
	coc_connect();
	return 0;
}

/**
 * GATT read callback for the reader SPSM characteristic: parse the flat payload to extract SPSM,
 * supported versions, and features. Verify that the peer publishes the selected version. Write the
 * selected version and a features byte to the device-version characteristic. On any error, abandon
 * this peer.
 */
static int on_spsm_read(uint16_t conn_handle, const struct ble_gatt_error *error,
			struct ble_gatt_attr *attr, void *arg)
{
	(void)arg;
	if (error->status != 0) {
		abandon("reader-SPSM read", error->status);
		return 0;
	}

	uint8_t buf[64];
	uint16_t len = 0;

	if (attr == NULL || attr->om == NULL ||
	    ble_hs_mbuf_to_flat(attr->om, buf, sizeof(buf), &len) != 0) {
		abandon("reader-SPSM read: flatten", 0);
		return 0;
	}
	if (aliro_ble_central_parse_read_payload(buf, len, &s_peer.peer) != 0) {
		abandon("reader-SPSM read: malformed payload", (int)len);
		return 0;
	}
	ESP_LOGI(TAG, "peer SPSM 0x%04x, %u version(s), features 0x%02x",
		 (unsigned)s_peer.peer.spsm, (unsigned)s_peer.peer.versions_count,
		 s_peer.peer.features);

	/* A version the peer does not publish is the worst failure shape here: the
	 * nRF reader's write handler returns SUCCESS but skips recording it
	 * (gatt_server.cpp:115), and the L2CAP accept hook gates on that record
	 * (aliro_service.cpp:256), so the only symptom is a refused CoC several
	 * steps later. Catch it while it can still be named. */
	bool supported = false;

	for (size_t i = 0; i < s_peer.peer.versions_count; i++) {
		if (s_peer.peer.versions[i] == s_cfg.selected_version) {
			supported = true;
			break;
		}
	}
	if (!supported) {
		abandon("peer does not publish our version", (int)s_cfg.selected_version);
		return 0;
	}

	/* Tell the reader which version we selected, then open the channel. The
	 * characteristic is [version_be16][features_len][features...]; both readers
	 * reject anything under 3 bytes (aliro_ble.c:315, gatt_server.cpp:85). We
	 * support none of the optional features (timesync procedures 0 and 1, LE
	 * Coded PHY), so the byte is zero — but it still has to be on the wire. */
	uint8_t sel[4] = {(uint8_t)(s_cfg.selected_version >> 8),
			  (uint8_t)(s_cfg.selected_version & 0xffu), 1u, 0u};
	int rc = ble_gattc_write_flat(conn_handle, s_peer.devver_val_handle, sel, sizeof(sel),
				      on_devver_write, NULL);

	if (rc != 0) {
		abandon("ble_gattc_write_flat", rc);
	}
	return 0;
}

/**
 * GATT characteristic discovery callback: record the val_handle of the SPSM and device-version
 * characteristics. On discovery completion, read the SPSM characteristic. On error, abandon this
 * peer and return to scanning.
 */
static int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
		       const struct ble_gatt_chr *chr, void *arg)
{
	(void)arg;
	if (error->status == 0 && chr != NULL) {
		if (ble_uuid_cmp(&chr->uuid.u, &k_chr_reader_spsm_uuid.u) == 0) {
			s_peer.spsm_val_handle = chr->val_handle;
		} else if (ble_uuid_cmp(&chr->uuid.u, &k_chr_device_ver_uuid.u) == 0) {
			s_peer.devver_val_handle = chr->val_handle;
		}
		return 0;
	}
	if (error->status != BLE_HS_EDONE) {
		abandon("characteristic discovery", error->status);
		return 0;
	}
	/* Discovery complete: both handles are required before we can proceed. */
	if (s_peer.spsm_val_handle == 0 || s_peer.devver_val_handle == 0) {
		abandon("peer is missing an Aliro characteristic", 0);
		return 0;
	}

	int rc = ble_gattc_read(conn_handle, s_peer.spsm_val_handle, on_spsm_read, NULL);

	if (rc != 0) {
		abandon("ble_gattc_read", rc);
	}
	return 0;
}

/**
 * GATT service discovery callback: record the service handle range for the 0xFFF2 Aliro service. On
 * discovery completion, discover all characteristics within that range. On error, abandon this
 * peer.
 */
static int on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
		       const struct ble_gatt_svc *service, void *arg)
{
	(void)arg;
	if (error->status == 0 && service != NULL) {
		s_peer.svc_start = service->start_handle;
		s_peer.svc_end = service->end_handle;
		return 0;
	}
	if (error->status != BLE_HS_EDONE) {
		abandon("service discovery", error->status);
		return 0;
	}
	if (s_peer.svc_start == 0) {
		abandon("peer has no 0xFFF2 service", 0);
		return 0;
	}

	int rc = ble_gattc_disc_all_chrs(conn_handle, s_peer.svc_start, s_peer.svc_end, on_chr_disc,
					 NULL);

	if (rc != 0) {
		abandon("ble_gattc_disc_all_chrs", rc);
	}
	return 0;
}

/* ---- scanning + connection ---- */

/* True when this advert carries Aliro service data from the reader we want. The
 * reader falls back to a bare UUID + name when unprovisioned or GRK-less
 * (aliro_ble.c:589); that form has no group id to match, so it is skipped
 * quietly rather than treated as an error. */
static bool advert_is_our_reader(const struct ble_hs_adv_fields *fields)
{
	struct aliro_ble_central_adv adv;

	if (fields->svc_data_uuid16 == NULL ||
	    fields->svc_data_uuid16_len != ALIRO_BLE_CENTRAL_SVC_DATA_LEN) {
		return false;
	}
	if (aliro_ble_central_parse_adv(fields->svc_data_uuid16, fields->svc_data_uuid16_len,
					&adv) != 0) {
		return false;
	}

	static const uint8_t k_zero_id[32] = {0};

	if (memcmp(s_cfg.reader_id, k_zero_id, sizeof(k_zero_id)) == 0) {
		/* Bench affordance: no reader identity provisioned yet, so take the
		 * first Aliro reader seen and log its group id for the operator to
		 * copy. A provisioned initiator never lands here. */
		ESP_LOGW(TAG,
			 "no reader_id set; latching onto group id "
			 "%02x%02x%02x%02x%02x%02x%02x%02x sub %02x%02x",
			 adv.group_id[0], adv.group_id[1], adv.group_id[2], adv.group_id[3],
			 adv.group_id[4], adv.group_id[5], adv.group_id[6], adv.group_id[7],
			 adv.sub_id[0], adv.sub_id[1]);
		return true;
	}
	return aliro_ble_central_adv_matches(&adv, s_cfg.reader_id) == 1;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
	(void)arg;
	switch (event->type) {
	case BLE_GAP_EVENT_DISC: {
		struct ble_hs_adv_fields fields;

		/* ble_gap_disc_cancel() does not retract reports the stack has already
		 * queued, so more DISC events can arrive after we decide to connect.
		 * Without this guard each one would fire another ble_gap_connect. */
		if (s_connecting) {
			return 0;
		}
		if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) !=
		    0) {
			return 0;
		}
		if (!advert_is_our_reader(&fields)) {
			return 0;
		}
		ESP_LOGI(TAG, "found our reader (rssi %d); connecting", event->disc.rssi);
		s_connecting = true;
		ble_gap_disc_cancel();

		int rc = ble_gap_connect(s_own_addr_type, &event->disc.addr, 10000, NULL, gap_event,
					 NULL);

		if (rc != 0) {
			abandon("ble_gap_connect", rc);
		}
		return 0;
	}

	case BLE_GAP_EVENT_CONNECT:
		if (event->connect.status != 0) {
			abandon("connect", event->connect.status);
			return 0;
		}
		s_peer.conn_handle = event->connect.conn_handle;
		ESP_LOGI(TAG, "connected (conn %u); discovering 0xFFF2",
			 event->connect.conn_handle);
		{
			int rc = ble_gattc_disc_svc_by_uuid(event->connect.conn_handle,
							    &k_svc_uuid.u, on_svc_disc, NULL);

			if (rc != 0) {
				abandon("ble_gattc_disc_svc_by_uuid", rc);
			}
		}
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
		ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
		if (s_cfg.cb.on_closed && s_peer.chan != NULL) {
			s_cfg.cb.on_closed(s_peer.conn_handle);
		}
		reset_peer();
		start_scan();
		return 0;

	default:
		return 0;
	}
}

/**
 * Configure and start BLE GAP active scanning with duplicate filtering disabled (the dynamic tag
 * changes). Return to scanning after connection or on error.
 */
static void start_scan(void)
{
	struct ble_gap_disc_params params;

	memset(&params, 0, sizeof(params));
	params.filter_duplicates = 0; /* the dynamic tag changes; never dedupe it away */
	params.passive = 0;

	s_connecting = false;

	int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);

	if (rc != 0 && rc != BLE_HS_EALREADY) {
		ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
		return;
	}
	ESP_LOGI(TAG, "scanning for the Aliro reader");
}

/* ---- host bring-up ---- */

static void on_sync(void)
{
	int rc = ble_hs_util_ensure_addr(0);

	if (rc != 0) {
		ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
		return;
	}
	rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
	if (rc != 0) {
		ESP_LOGE(TAG, "infer_auto rc=%d", rc);
		return;
	}
	ESP_LOGI(TAG, "NimBLE synced; running as Aliro initiator");
	start_scan();
}

/**
 * Log that the NimBLE stack has reset and include the reset reason code.
 */
static void on_reset(int reason)
{
	ESP_LOGW(TAG, "NimBLE reset; reason=%d", reason);
}

/**
 * Run the NimBLE host event loop in the current thread until shutdown, then deinitialize the
 * FreeRTOS integration.
 */
static void host_task(void *param)
{
	(void)param;
	nimble_port_run();
	nimble_port_freertos_deinit();
}

/**
 * Initialize the mbuf pool for CoC data: create a memory pool and mbuf pool with the configured
 * buffer count and MTU. Return 0 on success or -1 if either pool initialization fails.
 */
static int coc_pools_init(void)
{
	int rc = os_mempool_init(&s_coc_mempool, ALIRO_COC_BUF_COUNT, ALIRO_L2CAP_MTU, s_coc_mem,
				 "aliro_coc_c");

	if (rc != 0) {
		ESP_LOGE(TAG, "coc mempool init rc=%d", rc);
		return -1;
	}
	rc = os_mbuf_pool_init(&s_coc_mbuf_pool, &s_coc_mempool, ALIRO_L2CAP_MTU,
			       ALIRO_COC_BUF_COUNT);
	if (rc != 0) {
		ESP_LOGE(TAG, "coc mbuf pool init rc=%d", rc);
		return -1;
	}
	return 0;
}

int aliro_ble_central_start(const struct aliro_ble_central_config *cfg)
{
	if (cfg == NULL || cfg->selected_version == 0) {
		return -1;
	}
	s_cfg = *cfg;
	reset_peer();

	esp_err_t err = nvs_flash_init();

	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	err = nimble_port_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nimble_port_init rc=%d", err);
		return -1;
	}

	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	ble_svc_gap_init();
	ble_svc_gatt_init();

	if (coc_pools_init() != 0) {
		return -1;
	}
	if (ble_svc_gap_device_name_set("Aliro Initiator") != 0) {
		ESP_LOGW(TAG, "device_name_set failed");
	}

	nimble_port_freertos_init(host_task);
	return 0;
}

/**
 * Send data to the peer over the L2CAP CoC: allocate an mbuf, append the data, and submit it via
 * ble_l2cap_send. Return 0 on success (stack owns the buffer or it is queued); return -1 on failure
 * (buffer freed on error).
 */
int aliro_ble_central_send(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0 || s_peer.chan == NULL || s_peer.conn_handle != conn_handle) {
		return -1;
	}

	struct os_mbuf *sdu = os_mbuf_get_pkthdr(&s_coc_mbuf_pool, 0);

	if (sdu == NULL) {
		return -1;
	}
	if (os_mbuf_append(sdu, data, len) != 0) {
		os_mbuf_free_chain(sdu);
		return -1;
	}

	int rc = ble_l2cap_send(s_peer.chan, sdu);

	if (rc == 0 || rc == BLE_HS_ESTALLED) {
		return 0; /* stack owns it; ESTALLED = queued, flushed on TX_UNSTALLED */
	}
	os_mbuf_free_chain(sdu);
	ESP_LOGW(TAG, "ble_l2cap_send rc=%d", rc);
	return -1;
}
