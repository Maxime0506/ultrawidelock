// Device-side (User-Device) BLE transport interface: the central/client mirror of
// ultrawidelock_ble.h. Where the reader advertises 0xFFF2, serves the GATT characteristics
// and runs an L2CAP CoC server, the initiator scans, connects, reads the reader's
// SPSM/versions, writes its selected version and opens a CoC client to that SPSM.
//
// The platform-free half (advert + READ-payload decoding, BleSK salt assembly)
// lives in ultrawidelock_ble_central.c and is host-testable; the NimBLE backend for the
// transport calls sits in ports/esp32, so a Zephyr bt_gap_*/bt_l2cap_* backend
// can be written behind this same header.
// Every byte layout here is the inverse of the reader's emitters in
// ports/esp32/components/ultrawidelock_ble/ultrawidelock_ble.c (build_aliro_svc_data,
// build_read_payload).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ultrawidelock_advtag.h" /* ULTRAWIDELOCK_ADVTAG_LEN */

#ifdef __cplusplus
extern "C" {
#endif

/* Most protocol versions we will record from a peer's GATT READ. The reader caps
 * its own advertised list at 8 (ULTRAWIDELOCK_MAX_VERSIONS in ultrawidelock_ble.c). */
#define ULTRAWIDELOCK_BLE_CENTRAL_MAX_VERSIONS 8u

/* Aliro service data is 26 B on air: the 2-byte 0xFFF2 UUID then 24 B payload. */
#define ULTRAWIDELOCK_BLE_CENTRAL_SVC_DATA_LEN 26u

/* Reader identity recovered from one 0xFFF2 service-data advert. Field order is
 * the inverse of build_aliro_svc_data (ultrawidelock_ble.c:532). */
struct ultrawidelock_ble_central_adv {
	uint8_t flags;       /* bit7 = BLE+UWB supported, bits2:0 = advert version */
	int8_t tx_power;     /* dBm as advertised */
	uint8_t group_id[8]; /* truncated reader group id  = reader_id[0..7] */
	uint8_t sub_id[2];   /* truncated reader group sub id = reader_id[16..17] */
	uint32_t expiry;     /* dynamic-tag expiry, 0xFFFFFFFF = reader has no clock */
	uint8_t tag[ULTRAWIDELOCK_ADVTAG_LEN];
};

/* What the peer publishes on the reader-SPSM characteristic
 * (D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3), read before the CoC opens. */
struct ultrawidelock_ble_central_peer {
	uint16_t spsm; /* dynamic-range PSM; NOT well-known, must come from the READ */
	uint16_t versions[ULTRAWIDELOCK_BLE_CENTRAL_MAX_VERSIONS]; /* reader_supported_versions */
	size_t versions_count;
	uint8_t features;
};

/* Decode 0xFFF2 service data. svc_data must be the full 26 B including the
 * leading little-endian UUID, which is checked. Returns 0, or -1 on a short
 * buffer or a UUID that is not 0xFFF2. */
int ultrawidelock_ble_central_parse_adv(const uint8_t *svc_data, size_t len,
				struct ultrawidelock_ble_central_adv *out);

/* True when this advert is from the reader we are provisioned against: the
 * advertised group id/sub id are truncations of reader_id, so compare against
 * reader_id[0..7] and reader_id[16..17] rather than the whole identifier. */
int ultrawidelock_ble_central_adv_matches(const struct ultrawidelock_ble_central_adv *adv,
				  const uint8_t reader_id[32]);

/* Decode the reader-SPSM READ payload, whose layout is
 * [spsm_be16][versions_len = 2N][version_be16 x N][features_len = 1][features].
 * Inverse of build_read_payload (ultrawidelock_ble.c:271). Returns 0, or -1 if the
 * buffer is short, a length field disagrees with the payload, the version list
 * is odd-length, or it holds more than ULTRAWIDELOCK_BLE_CENTRAL_MAX_VERSIONS entries. */
int ultrawidelock_ble_central_parse_read_payload(const uint8_t *payload, size_t len,
					 struct ultrawidelock_ble_central_peer *out);

/* Build the BleSK HKDF salt: reader_supported_versions || selected_version, each
 * big-endian (§11.8.1). The peer's list comes from the GATT READ above, so a
 * multi-version reader is handled without hardcoding — this is the input the
 * reader itself derives from k_proto_versions (ultrawidelock_reader.c:390). Needs
 * 2 * (versions_count + 1) bytes of capacity. Returns 0 or -1. */
int ultrawidelock_ble_central_blesk_salt(const struct ultrawidelock_ble_central_peer *peer,
					 uint16_t selected, uint8_t *out, size_t cap,
					 size_t *out_len);

/* ---- transport (backend-provided; NimBLE one lives in ports/esp32) ---- */

/**
 * Callbacks for BLE central transport: on_ready when CoC opens and peer facts are known, on_data
 * for each inbound SDU, on_closed when link or CoC drops.
 */
struct ultrawidelock_ble_central_callbacks {
	/* The CoC is open and the peer's GATT facts are known: the Aliro
	 * transaction can start. peer stays valid only for the call. */
	void (*on_ready)(uint16_t conn_handle, const struct ultrawidelock_ble_central_peer *peer);
	/* One inbound SDU from the reader (an AP response or a sealed ranging SDU). */
	void (*on_data)(uint16_t conn_handle, const uint8_t *data, size_t len);
	/* CoC or link dropped; any session state keyed on conn_handle is dead. */
	void (*on_closed)(uint16_t conn_handle);
};

/**
 * Configuration for BLE central transport: reader_id to match by truncated group/sub ID in adverts,
 * selected_version for device-version characteristic, and callbacks.
 */
struct ultrawidelock_ble_central_config {
	/* The reader we are provisioned against; scanning matches its advert by
	 * the truncated group id/sub id (see ultrawidelock_ble_central_adv_matches). */
	uint8_t reader_id[32];
	uint16_t selected_version; /* written to the device-version characteristic */
	struct ultrawidelock_ble_central_callbacks cb;
};

/* Bring up the BLE host in the central role and start scanning for the
 * configured reader. Drives connect -> GATT discovery -> READ/WRITE -> CoC and
 * reports through cfg->cb. Returns 0 once the host is running, <0 on setup
 * failure; discovery outcomes arrive via the callbacks. */
int ultrawidelock_ble_central_start(const struct ultrawidelock_ble_central_config *cfg);

/* Send one SDU to the reader over the open CoC. Returns 0 on success (including
 * the queued-on-stall case), <0 if there is no channel or the send fails. */
int ultrawidelock_ble_central_send(uint16_t conn_handle, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
