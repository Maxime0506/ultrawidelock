// Zephyr central/client backend for the Aliro initiator: the mirror of
// aliro_ble_zephyr.c. That file advertises 0xFFF2, serves the characteristics and
// runs a CoC server; this one scans for 0xFFF2, connects, discovers, reads the
// reader's SPSM/versions, writes the selected version and opens a CoC client to
// that SPSM.
/*
 * Zephyr backend behind the shared ultrawidelock_ble_central.h, the counterpart of the
 * NimBLE one in ports/esp32/components/ultrawidelock_ble_central. The decoding it feeds
 * on (advert, READ payload, BleSK salt) is platform-free and lives in
 * modules/ultrawidelock_cred/src/ultrawidelock_ble_central.c, host-tested separately; everything
 * here is stack plumbing that only silicon can exercise.
 *
 * Bring-up is one linear chain, each step resumed from the previous callback:
 *   bt_enable -> scan -> match advert -> connect -> discover service ->
 *   discover characteristics -> READ spsm/versions -> WRITE our version ->
 *   L2CAP CoC connect -> on_ready
 * Any failure logs and returns to scanning, so a reader that reboots mid-chain
 * is picked up again without intervention.
 */
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "ultrawidelock_ble_central.h"

LOG_MODULE_REGISTER(ultrawidelock_central, CONFIG_LOG_DEFAULT_LEVEL);

/* Sized to match the reader side (aliro_ble_zephyr.c:49) so an SDU that fits one
 * fits the other. The SPSM itself is NOT hardcoded here: it is whatever the READ
 * returns, because it sits in the dynamic range and is the reader's to choose. */
#define ULTRAWIDELOCK_L2CAP_MTU 512u

/* Aliro service, 16-bit 0xFFF2 — the one the reader advertises and serves. */
static const struct bt_uuid_16 k_svc_uuid = BT_UUID_INIT_16(0xfff2u);

/* Reader SPSM + BLE-UWB protocol version, D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3. */
static const struct bt_uuid_128 k_chr_reader_spsm_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xd3b5a130, 0x9e23, 0x4b3a, 0x8be4, 0x6b1ee5f980a3));

/* User-device selected BLE-UWB protocol version, BD4B9502-3F54-11EC-B919-0242AC120005. */
static const struct bt_uuid_128 k_chr_device_ver_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xbd4b9502, 0x3f54, 0x11ec, 0xb919, 0x0242ac120005));

static struct ultrawidelock_ble_central_config s_cfg;

/* Set the moment we decide to connect, cleared when we go back to scanning.
 * bt_le_scan_stop() does not retract reports the stack has already queued, so
 * without this guard each late report would fire another bt_conn_le_create. */
static bool s_connecting;

/* One peer at a time. CONFIG_BT_MAX_CONN=1 makes that a build-time fact, not a
 * hope, so a single record is the whole table. Filled in as the chain advances. */
static struct {
	struct bt_conn *conn;
	uint16_t svc_end;
	uint16_t spsm_val_handle;
	uint16_t devver_val_handle;
	bool coc_open;
	struct ultrawidelock_ble_central_peer peer;
} s_peer;

/* Separate pools by direction, same reasoning as the reader side: sharing one
 * would let a queued outbound SDU starve the receive path mid-transaction. */
NET_BUF_POOL_FIXED_DEFINE(s_coc_rx_pool, 2, BT_L2CAP_SDU_BUF_SIZE(ULTRAWIDELOCK_L2CAP_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(s_coc_tx_pool, 2, BT_L2CAP_SDU_BUF_SIZE(ULTRAWIDELOCK_L2CAP_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct aliro_coc_client {
	struct bt_l2cap_le_chan le;
	bool in_use;
} s_coc;

/* Zephyr's async GATT calls keep a pointer to these across the whole exchange,
 * so they cannot be stack locals. One peer at a time makes one copy enough. */
static struct bt_gatt_discover_params s_disc;
static struct bt_gatt_read_params s_read;
static struct bt_gatt_write_params s_write;
static uint8_t s_sel_payload[4];

static void start_scan(void);

/* The engine's transport handle. Zephyr identifies a link by pointer, the seam
 * by uint16_t, so hand out the connection index (0..MAX_CONN-1), exactly as
 * aliro_ble_zephyr.c:104 does. */
static uint16_t conn_to_handle(struct bt_conn *conn)
{
	return (uint16_t)bt_conn_index(conn);
}

/**
 * Drop any reference held on the peer connection and clear all per-peer state.
 */
static void reset_peer(void)
{
	if (s_peer.conn != NULL) {
		bt_conn_unref(s_peer.conn);
	}
	memset(&s_peer, 0, sizeof(s_peer));
	s_coc.in_use = false;
}

/* Abandon this peer and go back to scanning. Called from every failure path so a
 * half-finished chain can never leave the app wedged. */
static void abandon(const char *why, int err)
{
	LOG_WRN("%s (err=%d); rescanning", why, err);
	if (s_peer.conn != NULL) {
		/* Disconnecting drops us into disconnected(), which resets and
		 * rescans. If the link is already gone this fails harmlessly and
		 * we fall through to doing it here. */
		if (bt_conn_disconnect(s_peer.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN) == 0) {
			return;
		}
	}
	reset_peer();
	start_scan();
}

/* ---- L2CAP CoC client ---------------------------------------------------- */

/**
 * Allocate a receive net_buf from the CoC pool with no wait.
 */
static struct net_buf *coc_alloc_buf(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	return net_buf_alloc(&s_coc_rx_pool, K_NO_WAIT);
}

/**
 * Forward one received SDU to the on_data callback as a transport handle and byte buffer.
 */
static int coc_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	if (s_cfg.cb.on_data != NULL) {
		s_cfg.cb.on_data(conn_to_handle(chan->conn), buf->data, buf->len);
	}
	return 0;
}

/**
 * Handle CoC establishment: the transaction can now start, so report on_ready with the peer facts
 * the GATT READ recovered.
 */
static void coc_connected(struct bt_l2cap_chan *chan)
{
	s_peer.coc_open = true;
	LOG_INF("coc connected (conn %u, SPSM 0x%04x)", conn_to_handle(chan->conn),
		(unsigned)s_peer.peer.spsm);
	if (s_cfg.cb.on_ready != NULL) {
		s_cfg.cb.on_ready(conn_to_handle(chan->conn), &s_peer.peer);
	}
}

/**
 * Handle CoC teardown by clearing channel state and notifying the app. The link itself may still be
 * up, so this does not rescan; disconnected() owns that.
 */
static void coc_disconnected(struct bt_l2cap_chan *chan)
{
	uint16_t handle = conn_to_handle(chan->conn);

	s_coc.in_use = false;
	s_peer.coc_open = false;
	LOG_INF("coc disconnected (conn %u)", handle);
	if (s_cfg.cb.on_closed != NULL) {
		s_cfg.cb.on_closed(handle);
	}
}

static const struct bt_l2cap_chan_ops k_coc_ops = {
	.alloc_buf = coc_alloc_buf,
	.recv = coc_recv,
	.connected = coc_connected,
	.disconnected = coc_disconnected,
};

/* Final step of the chain: open the CoC to the SPSM the READ gave us. */
static void coc_connect(void)
{
	if (s_coc.in_use) {
		abandon("coc already in use", 0);
		return;
	}
	memset(&s_coc.le, 0, sizeof(s_coc.le));
	s_coc.le.chan.ops = &k_coc_ops;
	s_coc.le.rx.mtu = ULTRAWIDELOCK_L2CAP_MTU;
	s_coc.in_use = true;

	int err = bt_l2cap_chan_connect(s_peer.conn, &s_coc.le.chan, s_peer.peer.spsm);

	if (err != 0) {
		s_coc.in_use = false;
		abandon("bt_l2cap_chan_connect", err);
	}
}

/* ---- GATT discovery chain ------------------------------------------------ */

/**
 * Callback when the device-version write completes. On success, open the CoC to the SPSM previously
 * read. On error, abandon this peer.
 */
static void on_devver_write(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (err != 0u) {
		abandon("device-version write", (int)err);
		return;
	}
	coc_connect();
}

/**
 * GATT read callback for the reader-SPSM characteristic: parse the payload for SPSM, supported
 * versions and features, check the peer publishes our version, then write the selected version.
 */
static uint8_t on_spsm_read(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			    const void *data, uint16_t length)
{
	ARG_UNUSED(params);

	if (err != 0u) {
		abandon("reader-SPSM read", (int)err);
		return BT_GATT_ITER_STOP;
	}
	if (data == NULL || length == 0u) {
		abandon("reader-SPSM read: empty", (int)length);
		return BT_GATT_ITER_STOP;
	}
	if (ultrawidelock_ble_central_parse_read_payload(data, length, &s_peer.peer) != 0) {
		abandon("reader-SPSM read: malformed payload", (int)length);
		return BT_GATT_ITER_STOP;
	}
	LOG_INF("peer SPSM 0x%04x, %u version(s), features 0x%02x", (unsigned)s_peer.peer.spsm,
		(unsigned)s_peer.peer.versions_count, s_peer.peer.features);

	/* A version the peer does not publish is the worst failure shape here: the
	 * nRF reader's write handler returns SUCCESS but skips recording it, and the
	 * L2CAP accept hook gates on that record, so the only symptom is a refused
	 * CoC several steps later. Catch it while it can still be named. */
	bool supported = false;

	for (size_t i = 0; i < s_peer.peer.versions_count; i++) {
		if (s_peer.peer.versions[i] == s_cfg.selected_version) {
			supported = true;
			break;
		}
	}
	if (!supported) {
		abandon("peer does not publish our version", (int)s_cfg.selected_version);
		return BT_GATT_ITER_STOP;
	}

	/* Tell the reader which version we selected, then open the channel. The
	 * characteristic is [version_be16][features_len][features...]; both readers
	 * reject anything under 3 bytes. We support none of the optional features
	 * (timesync procedures 0 and 1, LE Coded PHY), so the byte is zero, but it
	 * still has to be on the wire. */
	s_sel_payload[0] = (uint8_t)(s_cfg.selected_version >> 8);
	s_sel_payload[1] = (uint8_t)(s_cfg.selected_version & 0xffu);
	s_sel_payload[2] = 1u;
	s_sel_payload[3] = 0u;

	memset(&s_write, 0, sizeof(s_write));
	s_write.func = on_devver_write;
	s_write.handle = s_peer.devver_val_handle;
	s_write.offset = 0u;
	s_write.data = s_sel_payload;
	s_write.length = (uint16_t)sizeof(s_sel_payload);

	int werr = bt_gatt_write(conn, &s_write);

	if (werr != 0) {
		abandon("bt_gatt_write", werr);
	}
	return BT_GATT_ITER_STOP;
}

/**
 * GATT characteristic discovery callback: record the value handles of the SPSM and device-version
 * characteristics, then read the SPSM one once discovery completes.
 */
static uint8_t on_chr_disc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	ARG_UNUSED(params);

	if (attr != NULL) {
		const struct bt_gatt_chrc *chrc = attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, &k_chr_reader_spsm_uuid.uuid) == 0) {
			s_peer.spsm_val_handle = chrc->value_handle;
		} else if (bt_uuid_cmp(chrc->uuid, &k_chr_device_ver_uuid.uuid) == 0) {
			s_peer.devver_val_handle = chrc->value_handle;
		}
		return BT_GATT_ITER_CONTINUE;
	}

	/* attr == NULL means discovery finished. Both handles are required. */
	if (s_peer.spsm_val_handle == 0u || s_peer.devver_val_handle == 0u) {
		abandon("peer is missing an Aliro characteristic", 0);
		return BT_GATT_ITER_STOP;
	}

	memset(&s_read, 0, sizeof(s_read));
	s_read.func = on_spsm_read;
	s_read.handle_count = 1u;
	s_read.single.handle = s_peer.spsm_val_handle;
	s_read.single.offset = 0u;

	int err = bt_gatt_read(conn, &s_read);

	if (err != 0) {
		abandon("bt_gatt_read", err);
	}
	return BT_GATT_ITER_STOP;
}

/**
 * GATT service discovery callback: record the 0xFFF2 service handle range, then discover the
 * characteristics inside it.
 */
static uint8_t on_svc_disc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	ARG_UNUSED(params);

	if (attr == NULL) {
		abandon("peer has no 0xFFF2 service", 0);
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_service_val *svc = attr->user_data;

	s_peer.svc_end = svc->end_handle;

	memset(&s_disc, 0, sizeof(s_disc));
	s_disc.uuid = NULL; /* all characteristics in range; we match by UUID ourselves */
	s_disc.func = on_chr_disc;
	s_disc.start_handle = attr->handle + 1u;
	s_disc.end_handle = svc->end_handle;
	s_disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	int err = bt_gatt_discover(conn, &s_disc);

	if (err != 0) {
		abandon("bt_gatt_discover characteristics", err);
	}
	return BT_GATT_ITER_STOP;
}

/* ---- scanning + connection ----------------------------------------------- */

/* True when this advert carries Aliro service data from the reader we want. The
 * reader falls back to a bare UUID + name when unprovisioned or GRK-less; that
 * form has no group id to match, so it is skipped quietly rather than treated as
 * an error. */
static bool advert_is_our_reader(struct bt_data *data, void *user_data)
{
	bool *matched = user_data;
	struct ultrawidelock_ble_central_adv adv;

	if (data->type != BT_DATA_SVC_DATA16 ||
	    data->data_len != ULTRAWIDELOCK_BLE_CENTRAL_SVC_DATA_LEN) {
		return true; /* keep walking the AD structures */
	}
	if (ultrawidelock_ble_central_parse_adv(data->data, data->data_len, &adv) != 0) {
		return true;
	}

	static const uint8_t k_zero_id[32] = {0};

	if (memcmp(s_cfg.reader_id, k_zero_id, sizeof(k_zero_id)) == 0) {
		/* Bench affordance: no reader identity provisioned yet, so take the
		 * first Aliro reader seen and log its group id for the operator to
		 * copy. A provisioned initiator never lands here. */
		LOG_WRN("no reader_id set; latching onto group id "
			"%02x%02x%02x%02x%02x%02x%02x%02x sub %02x%02x",
			adv.group_id[0], adv.group_id[1], adv.group_id[2], adv.group_id[3],
			adv.group_id[4], adv.group_id[5], adv.group_id[6], adv.group_id[7],
			adv.sub_id[0], adv.sub_id[1]);
		*matched = true;
		return false;
	}
	if (ultrawidelock_ble_central_adv_matches(&adv, s_cfg.reader_id) == 1) {
		*matched = true;
		return false;
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *ad)
{
	ARG_UNUSED(adv_type);

	/* bt_le_scan_stop() does not retract reports the stack has already queued,
	 * so more can arrive after we decide to connect. Without this guard each one
	 * would fire another bt_conn_le_create. */
	if (s_connecting) {
		return;
	}

	bool matched = false;

	/* bt_data_parse consumes the buffer, so hand it a copy: a non-matching
	 * advert must leave the caller's net_buf_simple intact for nothing, but a
	 * second parse of the same report would otherwise see an empty buffer. */
	struct net_buf_simple copy = *ad;

	bt_data_parse(&copy, advert_is_our_reader, &matched);
	if (!matched) {
		return;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("found our reader %s (rssi %d); connecting", addr_str, rssi);

	s_connecting = true;

	int err = bt_le_scan_stop();

	if (err != 0 && err != -EALREADY) {
		LOG_WRN("bt_le_scan_stop err=%d", err);
	}

	struct bt_conn *conn = NULL;

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);
	if (err != 0) {
		abandon("bt_conn_le_create", err);
		return;
	}
	/* connected() takes its own reference; this one is the create ref. */
	s_peer.conn = conn;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0u) {
		abandon("connect", (int)err);
		return;
	}
	if (s_peer.conn == NULL) {
		s_peer.conn = bt_conn_ref(conn);
	}
	LOG_INF("connected (conn %u); discovering 0xFFF2", conn_to_handle(conn));

	memset(&s_disc, 0, sizeof(s_disc));
	s_disc.uuid = &k_svc_uuid.uuid;
	s_disc.func = on_svc_disc;
	s_disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	s_disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	s_disc.type = BT_GATT_DISCOVER_PRIMARY;

	int derr = bt_gatt_discover(conn, &s_disc);

	if (derr != 0) {
		abandon("bt_gatt_discover primary", derr);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("disconnected (conn %u, reason 0x%02x)", conn_to_handle(conn), reason);

	/* coc_disconnected() fires first when the channel was open, so on_closed is
	 * already delivered in that case. Cover the link-dropped-mid-chain case. */
	if (s_peer.coc_open && s_cfg.cb.on_closed != NULL) {
		s_cfg.cb.on_closed(conn_to_handle(conn));
	}
	reset_peer();
	start_scan();
}

BT_CONN_CB_DEFINE(aliro_central_conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

/**
 * Start active scanning. Duplicate filtering stays off because the reader's dynamic advert tag
 * changes and dedupe would hide the very reports we match on.
 */
static void start_scan(void)
{
	static const struct bt_le_scan_param k_scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	s_connecting = false;

	int err = bt_le_scan_start(&k_scan_param, scan_cb);

	if (err != 0 && err != -EALREADY) {
		LOG_ERR("bt_le_scan_start err=%d", err);
		return;
	}
	LOG_INF("scanning for the Aliro reader");
}

/* ---- host bring-up ------------------------------------------------------- */

static void bt_ready(int err)
{
	if (err != 0) {
		LOG_ERR("bt_enable err=%d", err);
		return;
	}
	LOG_INF("Bluetooth ready; running as Aliro initiator");
	start_scan();
}

int ultrawidelock_ble_central_start(const struct ultrawidelock_ble_central_config *cfg)
{
	if (cfg == NULL || cfg->selected_version == 0u) {
		return -1;
	}
	s_cfg = *cfg;
	memset(&s_peer, 0, sizeof(s_peer));
	s_coc.in_use = false;

	int err = bt_enable(bt_ready);

	if (err == -EALREADY) {
		/* Someone else already brought the host up (the Matter stack does
		 * this on the reader image). Nothing to wait for, so scan now. */
		start_scan();
		return 0;
	}
	if (err != 0) {
		LOG_ERR("bt_enable err=%d", err);
		return -1;
	}
	return 0;
}

int ultrawidelock_ble_central_send(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0u || !s_peer.coc_open || s_peer.conn == NULL ||
	    conn_to_handle(s_peer.conn) != conn_handle) {
		return -1;
	}
	if (len > ULTRAWIDELOCK_L2CAP_MTU) {
		return -1;
	}

	struct net_buf *buf = net_buf_alloc(&s_coc_tx_pool, K_NO_WAIT);

	if (buf == NULL) {
		LOG_WRN("coc: out of tx buffers");
		return -1;
	}
	net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, data, len);

	int err = bt_l2cap_chan_send(&s_coc.le.chan, buf);

	if (err < 0) {
		net_buf_unref(buf);
		LOG_WRN("bt_l2cap_chan_send err=%d", err);
		return -1;
	}
	return 0;
}
