/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_ble — Zephyr/NCS backend for the Aliro BLE transport seam
 * (modules/woz_aliro/include/aliro_ble.h), for the DWM3001CDK standalone
 * reader. The byte contract is the one the ESP32-S3 NimBLE backend already
 * ships and the host tests already pin: the 0xFFF2 service, the reader-SPSM
 * READ payload, the device-version WRITE, and the Aliro transaction on an
 * L2CAP CoC at the published SPSM.
 *
 * Deliberately not here yet (stage 4 finishes them; none affects code size
 * materially, all are small additions on top of this skeleton):
 *   - the periodic dynamic-tag refresh (the ESP side re-derives on a callout),
 *   - the connection-RSSI poll that feeds the reader's ranging power gate,
 *   - attach mode, which only exists so the ESP32 reader can share a host with
 *     esp-matter. Nothing shares this host, so it stays -ENOTSUP.
 */
#include <string.h>
#include <time.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "aliro_advtag.h"
#include "aliro_ble.h"
#include "aliro_prov.h" /* ALIRO_GRK_LEN */
#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
#include "matter_ble_zephyr.h"
#include "matter_commission.h" /* the commissionable payload, when unprovisioned */
#endif

LOG_MODULE_REGISTER(aliro_ble, CONFIG_LOG_DEFAULT_LEVEL);

/* Same value the ESP32-S3 backend publishes (ports/esp32/components/aliro_ble/
 * aliro_ble.c:42). The dynamic-PSM range is 0x0080..0x00FF and the peer learns
 * the value from the READ characteristic, so it is ours to pick — but picking
 * the same one keeps bench captures comparable across the two ports. */
#define ALIRO_L2CAP_SPSM 0x0080u
#define ALIRO_L2CAP_MTU  512u

/* Reader SPSM + BLE-UWB protocol version, D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3. */
static const struct bt_uuid_128 k_chr_reader_spsm_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xd3b5a130, 0x9e23, 0x4b3a, 0x8be4, 0x6b1ee5f980a3));

/* User-device selected BLE-UWB protocol version, BD4B9502-3F54-11EC-B919-0242AC120005. */
static const struct bt_uuid_128 k_chr_device_ver_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xbd4b9502, 0x3f54, 0x11ec, 0xb919, 0x0242ac120005));

#define ALIRO_MAX_VERSIONS 8u

static uint16_t s_versions[ALIRO_MAX_VERSIONS];
static size_t s_versions_count;
static struct aliro_ble_callbacks s_cb;

/* Prebuilt READ payload: [SPSM be16][verLen u8][versions be16*N][featLen u8][features u8]. */
static uint8_t s_read_payload[2u + 1u + (2u * ALIRO_MAX_VERSIONS) + 1u + 1u];
static uint16_t s_read_payload_len;

/* Resolvable advertising params, set once the reader is provisioned. */
static bool s_adv_aliro;
/** Whether the one connection slot is occupied; see aliro_advertise(). */
static bool s_conn_up;
static uint8_t s_adv_group_id[8];
static uint8_t s_adv_sub_id[2];
static uint8_t s_adv_grk[ALIRO_GRK_LEN];
static int8_t s_adv_tx_power;

/* A dynamic tag whose expiry is in the phone's past is silently ignored, so a
 * clock we cannot trust must advertise the "unavailable" form instead. Mirrors
 * the ESP backend's ALIRO_ADV_TIME_FLOOR / ALIRO_ADV_TAG_VALID_S. */
#define ALIRO_ADV_TIME_FLOOR   1700000000
#define ALIRO_ADV_TAG_VALID_S  600

/* ---- L2CAP CoC: the Aliro transaction channel ---------------------------- */

/* One peer at a time. CONFIG_BT_MAX_CONN=1 makes that a build-time fact, not a
 * hope, so a single channel record is the whole table. */
static struct aliro_coc {
	struct bt_l2cap_le_chan le;
	struct bt_conn *conn;
	bool in_use;
} s_coc;

/* Separate pools by direction. Sharing one would let a queued outbound SDU
 * starve the receive path mid-transaction, which is the phase where a dropped
 * SDU costs the whole walk-up. */
NET_BUF_POOL_FIXED_DEFINE(s_coc_rx_pool, 2, BT_L2CAP_SDU_BUF_SIZE(ALIRO_L2CAP_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(s_coc_tx_pool, 2, BT_L2CAP_SDU_BUF_SIZE(ALIRO_L2CAP_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

/* The reader engine's transport handle. Zephyr identifies a link by pointer,
 * the seam by uint16_t, so hand out the connection index (0..MAX_CONN-1). */
static uint16_t conn_to_handle(struct bt_conn *conn)
{
	return (uint16_t)bt_conn_index(conn);
}

static struct net_buf *coc_alloc_buf(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	return net_buf_alloc(&s_coc_rx_pool, K_NO_WAIT);
}

static int coc_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	if (s_cb.on_data != NULL) {
		s_cb.on_data(conn_to_handle(chan->conn), buf->data, buf->len);
	}
	return 0;
}

static void coc_connected(struct bt_l2cap_chan *chan)
{
	LOG_INF("L2CAP CoC open (SPSM 0x%04x)", (unsigned)ALIRO_L2CAP_SPSM);
	if (s_cb.on_connected != NULL) {
		s_cb.on_connected(conn_to_handle(chan->conn));
	}
}

static void coc_disconnected(struct bt_l2cap_chan *chan)
{
	uint16_t handle = conn_to_handle(chan->conn);

	s_coc.in_use = false;
	s_coc.conn = NULL;
	LOG_INF("L2CAP CoC closed");
	if (s_cb.on_disconnected != NULL) {
		s_cb.on_disconnected(handle);
	}
}

static const struct bt_l2cap_chan_ops k_coc_ops = {
	.alloc_buf = coc_alloc_buf,
	.recv = coc_recv,
	.connected = coc_connected,
	.disconnected = coc_disconnected,
};

static int coc_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
		      struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(server);

	if (s_coc.in_use) {
		return -ENOMEM;
	}
	memset(&s_coc.le, 0, sizeof(s_coc.le));
	s_coc.le.chan.ops = &k_coc_ops;
	s_coc.le.rx.mtu = ALIRO_L2CAP_MTU;
	s_coc.conn = conn;
	s_coc.in_use = true;
	*chan = &s_coc.le.chan;
	return 0;
}

static struct bt_l2cap_server s_l2cap_server = {
	.psm = ALIRO_L2CAP_SPSM,
	.sec_level = BT_SECURITY_L1, /* Aliro encrypts at the application layer */
	.accept = coc_accept,
};

/* ---- GATT: reader-SPSM READ + device-version WRITE ------------------------ */

static uint8_t encode_features(const struct aliro_ble_features *f)
{
	uint8_t b = 0;

	if (f->timesync_procedure_0) {
		b |= (uint8_t)(1u << 0);
	}
	if (f->timesync_procedure_1) {
		b |= (uint8_t)(1u << 1);
	}
	if (f->le_coded_phy) {
		b |= (uint8_t)(1u << 2);
	}
	return b;
}

static void build_read_payload(const struct aliro_ble_config *cfg)
{
	uint8_t *p = s_read_payload;

	*p++ = (uint8_t)(ALIRO_L2CAP_SPSM >> 8);
	*p++ = (uint8_t)(ALIRO_L2CAP_SPSM & 0xffu);

	*p++ = (uint8_t)(s_versions_count * 2u);
	for (size_t i = 0; i < s_versions_count; i++) {
		*p++ = (uint8_t)(s_versions[i] >> 8);
		*p++ = (uint8_t)(s_versions[i] & 0xffu);
	}

	*p++ = 1u; /* features length: SupportedFeatures is one packed byte */
	*p++ = encode_features(&cfg->features);

	s_read_payload_len = (uint16_t)(p - s_read_payload);
}

static ssize_t reader_spsm_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	ARG_UNUSED(conn);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, s_read_payload, s_read_payload_len);
}

/* The peer writes the BLE-UWB protocol version it selected. Both shipped
 * readers require at least 3 bytes here (see 588df2e); we only need to accept
 * it, the reader engine reads the selection off the transaction itself. */
static ssize_t device_ver_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(buf);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	return len;
}

/* No _ENC / _AUTHEN on either permission: Aliro runs its own application-layer
 * secure channel, and requiring BLE bonding here would break the walk-up. The
 * shipped ESP32 reader is unpaired for the same reason and unlocks a real
 * iPhone Wallet key. */
BT_GATT_SERVICE_DEFINE(aliro_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(0xFFF2)),
	BT_GATT_CHARACTERISTIC(&k_chr_reader_spsm_uuid.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, reader_spsm_read, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&k_chr_device_ver_uuid.uuid, BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, device_ver_write, NULL),
);

/* ---- advertising --------------------------------------------------------- */

/* Aliro 1.0 section 11.3 (Table 11-2). 24 payload bytes after the 16-bit UUID:
 *   [0]      flags: bit7 = BLE+UWB supported, bits2:0 = version (0)
 *   [1]      tx power (int8)
 *   [2..9]   truncated reader group id      = reader_id[0..7]
 *   [10..11] truncated reader group sub id  = reader_id[16..17]
 *   [12..15] dynamic-tag expiry, big-endian (0xFFFFFFFF = no clock)
 *   [16]     reserved
 *   [17..23] dynamic tag
 * The derivation wants the identity address MSB-first; bt_id_get hands it out
 * LSB-first, same as NimBLE.
 */
static bool build_aliro_svc_data(uint8_t out[24])
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	bt_id_get(addrs, &count);
	if (count == 0) {
		LOG_WRN("adv: no identity address for the dynamic tag");
		return false;
	}

	uint8_t adva_msb[6];

	for (int i = 0; i < 6; i++) {
		adva_msb[i] = addrs[0].a.val[5 - i];
	}

	uint32_t expiry = ALIRO_ADVTAG_EXPIRY_UNAVAILABLE;
	time_t now = time(NULL);

	if (now >= ALIRO_ADV_TIME_FLOOR) {
		expiry = (uint32_t)now + ALIRO_ADV_TAG_VALID_S;
	}

	uint8_t dyn_tag[ALIRO_ADVTAG_LEN];
	int rc = aliro_advtag_derive(s_adv_grk, adva_msb, expiry, dyn_tag);

	if (rc != 0) {
		LOG_ERR("adv: dynamic-tag derive rc=%d", rc);
		return false;
	}

	uint8_t *p = out;

	*p++ = 0x80u; /* flags: BLE+UWB supported, version 0 */
	*p++ = (uint8_t)s_adv_tx_power;
	memcpy(p, s_adv_group_id, sizeof(s_adv_group_id));
	p += sizeof(s_adv_group_id);
	memcpy(p, s_adv_sub_id, sizeof(s_adv_sub_id));
	p += sizeof(s_adv_sub_id);
	*p++ = (uint8_t)(expiry >> 24);
	*p++ = (uint8_t)(expiry >> 16);
	*p++ = (uint8_t)(expiry >> 8);
	*p++ = (uint8_t)expiry;
	*p++ = 0x00u; /* reserved */
	memcpy(p, dyn_tag, ALIRO_ADVTAG_LEN);
	return true;
}

static int aliro_advertise(void)
{
	static uint8_t svc_data[2 + 24]; /* BT_DATA_SVC_DATA16 carries the UUID inline */
	struct bt_data ad[3];
	size_t ad_len;
	bool as_reader;

	svc_data[0] = 0xF2u; /* 0xFFF2, little-endian */
	svc_data[1] = 0xFFu;

	ad[0] = (struct bt_data)BT_DATA_BYTES(BT_DATA_FLAGS,
					      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR);

	/*
	 * Being FINDABLE outranks being approach-resolvable. Only one of the
	 * two payloads fits a legacy advert, and a node with no fabric that
	 * advertises as a reader cannot be commissioned at all -- which is
	 * what happened to a board the moment SetAliroReaderConfig succeeded
	 * and the pairing that delivered it then failed: provisioned, no
	 * fabric, and gone from Add Accessory with no way back but an erase.
	 */
#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
	bool commissioned = matter_commission_has_fabric();
#else
	bool commissioned = true;
#endif

	if (commissioned && s_adv_aliro && build_aliro_svc_data(&svc_data[2])) {
		ad[1] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, svc_data, sizeof(svc_data));
		ad_len = 2;
		as_reader = true;
	} else {
		as_reader = false;
		/* Unprovisioned / no GRK: the bare service UUID. A phone cannot
		 * approach-resolve this, but a scanner can see the reader. */
		static const uint8_t uuid16[2] = {0xF2u, 0xFFu};

		ad[1] = (struct bt_data)BT_DATA(BT_DATA_UUID16_ALL, uuid16, sizeof(uuid16));
		ad_len = 2;

#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
		/* A reader with no identity cannot unlock anything, so the only
		 * useful thing it can advertise is that it wants commissioning.
		 * Both elements fit one legacy packet: flags 3 + Matter service
		 * data 12 + the Aliro UUID 4 = 19 of the 31 bytes available, so
		 * the scanner affordance above is kept rather than traded away.
		 *
		 * This is also why there is no second advertising set. See
		 * matter_ble_commissionable_svc_data() for the 24.8 KB that
		 * CONFIG_BT_EXT_ADV would have cost. */
		static uint8_t matter_svc_data[MATTER_BLE_SVC_DATA_LEN];

		if (matter_ble_commissionable_svc_data(matter_svc_data, sizeof(matter_svc_data)) ==
		    0) {
			ad[2] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, matter_svc_data,
							sizeof(matter_svc_data));
			ad_len = 3;
		}
#endif
	}

	/*
	 * CONNECTABLE advertising needs a free connection object, and this board
	 * is built with exactly one (CONFIG_BT_MAX_CONN=1). While a commissioner
	 * holds it, bt_le_adv_start() can only return -ENOMEM -- which it did, on
	 * hardware, when SetAliroReaderConfig refreshed the payload mid-CASE, and
	 * the honest-looking "the reader is now invisible" below was wrong: the
	 * board was connected, not invisible.
	 *
	 * Nothing is lost by waiting. on_disconnected() schedules a readvertise,
	 * which rebuilds the payload from whatever the identity is by then.
	 */
	if (s_conn_up) {
		LOG_INF("advertising deferred: connected; the payload lands on disconnect");
		return 0;
	}

	int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ad_len, NULL, 0);

	if (rc == -EALREADY) {
		bt_le_adv_stop();
		rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ad_len, NULL, 0);
	}

	/* Which of the two the board is offering is the first thing to check on
	 * a bench, and it is not otherwise visible without a sniffer. */
	if (rc == 0) {
		LOG_INF("advertising: %s (%u AD elements)",
			as_reader ? "Aliro reader 0xFFF2" : "unprovisioned, commissionable",
			(unsigned int)ad_len);
	} else {
		/*
		 * LOUD. This used to log only on success, so a failed restart
		 * left the reader invisible with nothing in the log but the
		 * "re-advertising" line that preceded it -- a board that had
		 * unlocked once and then ignored every approach, while Matter
		 * kept answering over Thread because that is not BLE. Do not
		 * make an advertising failure quiet again.
		 */
		LOG_ERR("advertising FAILED to start (%d); the reader is now invisible", rc);
	}
	return rc;
}

/*
 * Restarting CONNECTABLE advertising from inside the disconnected callback
 * fails: Zephyr has not released the connection object yet at that point and
 * bt_le_adv_start() has no slot to give. The retry is what actually gets the
 * reader back, so it runs off the callback rather than in it.
 */
static void readvertise_work_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(s_readvertise_work, readvertise_work_fn);

static void readvertise_work_fn(struct k_work *w)
{
	/*
	 * Rescheduled rather than retried in a loop: this runs on the system
	 * work queue, which is also where the Aliro access protocol runs, so
	 * sleeping here would stall an unlock to fix advertising.
	 */
	static uint8_t attempts;

	ARG_UNUSED(w);

	if (aliro_advertise() == 0) {
		attempts = 0u;
		return;
	}
	if (++attempts < 5u) {
		(void)k_work_schedule(&s_readvertise_work, K_MSEC(100));
		return;
	}
	attempts = 0u;
	LOG_ERR("cannot resume advertising after a disconnect; a reset is needed");
}

/*
 * A phone that sends CONNECT_IND and is then never heard from again.
 *
 * The controller reports 0x3E once the establishment window (six connection
 * events) passes with no packet received, and the host prints its own line per
 * attempt -- 13 of them inside 4.3 s on 2026-08-02, then a normal connection.
 * Individually those lines say nothing: one is ordinary RF, a RUN of them is a
 * board that could not answer, and the two look identical unless the run is
 * counted. So count it, and report the run when it ends.
 */
static uint16_t s_estab_fails;
static uint32_t s_estab_first_ms;

/*
 * THE A/B. Change this number, flash, repeat the same walk-up and walk-away,
 * and compare the run line above.
 *
 *   50 (B, here)  what shipped before fa8f5e2
 *    0 (A)        what fa8f5e2 changed it to
 *
 * fa8f5e2 dropped the wait on the argument that a 0x3E never carried a byte, so
 * time spent not advertising is time the phone cannot find us. The measurement
 * that followed does not obviously support it. Bursts predate the change -- 13
 * and 8 failures -- but at 50 ms their gaps were 233 to 362 ms, consistent with
 * six connection events plus the wait; at 0 ms the same burst produced gaps of
 * 125 ms, SHORTER than six connection events can take. Those are not
 * establishment timeouts, so something else is ending them early.
 *
 * The suspicion this tests: restarting an advertising set tears down and
 * re-schedules radio activity, and a CONNECT_IND landing in that window gets a
 * connection with no valid anchor. Restarting instantly makes that window come
 * round more often.
 *
 * If B has fewer failures and slower gaps, the restart race is real and the
 * instant re-advertise has to go. If B differs only in pace, this is innocent
 * and the cause is RF or single-core contention with the DW3110, which needs
 * instrumentation rather than a knob.
 */
#define READVERTISE_AFTER_ESTAB_FAIL_MS 50

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	/*
	 * NOT where the run is reported, which took a hardware run to learn.
	 * The controller completes the connection first and only discovers the
	 * establishment failure afterwards, so this fires with err == 0 for
	 * EVERY attempt in a run -- including the ones about to fail. Counting
	 * the run here turned eight consecutive failures into eight runs of one.
	 */
	if (err == 0u) {
		s_conn_up = true;
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	s_conn_up = false;
	if (reason == BT_HCI_ERR_CONN_FAIL_TO_ESTAB) {
		if (s_estab_fails == 0u) {
			s_estab_first_ms = k_uptime_get_32();
		}
		s_estab_fails++;
	} else if (s_estab_fails > 0u) {
		/*
		 * Any other reason means the link carried something before it
		 * ended, so the run is over and its length is worth having in
		 * one line. 0x3E connections are the ones that never lived.
		 */
		LOG_WRN("%u connection(s) never established over %u ms before this one",
			(unsigned int)s_estab_fails,
			(unsigned int)(k_uptime_get_32() - s_estab_first_ms));
		s_estab_fails = 0u;
	}
	LOG_INF("BLE disconnected (0x%02x); re-advertising", reason);
	(void)k_work_schedule(&s_readvertise_work,
			      reason == BT_HCI_ERR_CONN_FAIL_TO_ESTAB
				      ? K_MSEC(READVERTISE_AFTER_ESTAB_FAIL_MS)
				      : K_MSEC(50));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

/* ---- the aliro_ble.h seam ------------------------------------------------ */

int aliro_ble_prepare(const struct aliro_ble_config *cfg)
{
	if (cfg == NULL || cfg->proto_versions == NULL || cfg->proto_versions_count == 0 ||
	    cfg->proto_versions_count > ALIRO_MAX_VERSIONS) {
		return -EINVAL;
	}
	s_versions_count = cfg->proto_versions_count;
	memcpy(s_versions, cfg->proto_versions, s_versions_count * sizeof(s_versions[0]));
	s_cb = cfg->cb;
	build_read_payload(cfg);
	return 0;
}

int aliro_ble_start(const struct aliro_ble_config *cfg)
{
	int rc = aliro_ble_prepare(cfg);

	if (rc != 0) {
		return rc;
	}

	/* Bring-up instrumentation: bt_enable blocks until the controller is up,
	 * and on nRF52 it will block FOREVER if the configured LFCLK source never
	 * starts. Bracketing it is the difference between knowing and guessing. */
	LOG_INF("bt_enable ...");
	rc = bt_enable(NULL);
	LOG_INF("bt_enable = %d", rc);
	if (rc != 0) {
		LOG_ERR("bt_enable rc=%d", rc);
		return rc;
	}

	rc = bt_l2cap_server_register(&s_l2cap_server);
	if (rc != 0) {
		LOG_ERR("l2cap server register rc=%d", rc);
		return rc;
	}

	rc = aliro_advertise();
	if (rc != 0) {
		LOG_ERR("adv start rc=%d", rc);
		return rc;
	}
	LOG_INF("Aliro reader up; advertising (SPSM 0x%04x)", (unsigned)ALIRO_L2CAP_SPSM);
	return 0;
}

uint16_t aliro_ble_spsm(void)
{
	return ALIRO_L2CAP_SPSM;
}

int aliro_ble_send(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	ARG_UNUSED(conn_handle);

	if (!s_coc.in_use) {
		return -ENOTCONN;
	}

	struct net_buf *buf = net_buf_alloc(&s_coc_tx_pool, K_MSEC(100));

	if (buf == NULL) {
		return -ENOMEM;
	}
	net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
	/*
	 * Checked here rather than left to net_buf's own assert, because
	 * CONFIG_ASSERT is not set in any image this board ships: net_buf_add()
	 * compiles its __ASSERT out, so an oversized len would run off the end
	 * of the pool buffer and corrupt whatever follows it, silently. The
	 * length comes from the reader's own encoded APDU rather than from the
	 * wire, so this is a guard on our own framing, not on an attacker --
	 * which is exactly the kind that disappears when the assert does.
	 */
	if (len > net_buf_tailroom(buf)) {
		LOG_ERR("APDU of %u B does not fit the CoC buffer (%u B tailroom)", (unsigned int)len,
			(unsigned int)net_buf_tailroom(buf));
		net_buf_unref(buf);
		return -EMSGSIZE;
	}
	net_buf_add_mem(buf, data, len);

	int rc = bt_l2cap_chan_send(&s_coc.le.chan, buf);

	if (rc < 0) {
		net_buf_unref(buf);
		return rc;
	}
	return 0;
}

int aliro_ble_disconnect(uint16_t conn_handle)
{
	ARG_UNUSED(conn_handle);

	if (s_coc.conn == NULL) {
		return -ENOTCONN;
	}
	return bt_conn_disconnect(s_coc.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void aliro_ble_set_adv_params(const uint8_t group_id8[8], const uint8_t sub_id2[2],
			      const uint8_t grk[16], int8_t tx_power)
{
	memcpy(s_adv_group_id, group_id8, sizeof(s_adv_group_id));
	memcpy(s_adv_sub_id, sub_id2, sizeof(s_adv_sub_id));
	memcpy(s_adv_grk, grk, sizeof(s_adv_grk));
	s_adv_tx_power = tx_power;
	s_adv_aliro = true;
}

void aliro_ble_readvertise(void)
{
	(void)aliro_advertise();
}

void aliro_ble_time_updated(void)
{
	(void)aliro_advertise();
}

/* The reader engine marshals these onto the transport's own task so a caller
 * elsewhere cannot race the BleSK counter. Zephyr's BLE callbacks all run on
 * the system workqueue, so posting to it is the same guarantee. */
static void (*s_status_cb)(bool);
static bool s_status_unsecured;
static void (*s_presence_cb)(void);

static void status_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (s_status_cb != NULL) {
		s_status_cb(s_status_unsecured);
	}
}
static K_WORK_DEFINE(s_status_work, status_work_fn);

static void presence_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (s_presence_cb != NULL) {
		s_presence_cb();
	}
}
static K_WORK_DEFINE(s_presence_work, presence_work_fn);

void aliro_ble_post_reader_status(void (*cb)(bool unsecured), bool unsecured)
{
	s_status_cb = cb;
	s_status_unsecured = unsecured;
	k_work_submit(&s_status_work);
}

void aliro_ble_post_presence_reset(void (*cb)(void))
{
	s_presence_cb = cb;
	k_work_submit(&s_presence_work);
}

/* Attach mode exists only so the ESP32 reader can share a NimBLE host with
 * esp-matter. Nothing shares this host. */
const struct ble_gatt_svc_def *aliro_ble_service_def(void)
{
	return NULL;
}

int aliro_ble_start_attached(void)
{
	return -ENOTSUP;
}
