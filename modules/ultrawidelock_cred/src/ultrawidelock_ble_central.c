// Platform-free half of the device-side BLE transport declared in
// ultrawidelock_ble_central.h: decodes the reader's 0xFFF2 service-data advert, decodes
// the reader-SPSM GATT READ payload (SPSM, supported protocol versions, feature
// mask), and assembles the BleSK salt from the version list the reader actually
// published rather than from a compiled-in constant. No BLE stack calls and no
// allocation, so it builds on the host and is checked byte for byte against the
// reader's own emitters.
/*
 * Platform-free half of the device-side BLE transport: decoding what our reader
 * publishes (the 0xFFF2 advert, the reader-SPSM READ payload) and assembling the
 * BleSK salt from it. No BLE stack calls, so it compiles on the host and is
 * KAT-testable against the reader's emitters. See ultrawidelock_ble_central.h.
 */
#include "ultrawidelock_ble_central.h"

#include <string.h>

/**
 * Parse a 26-byte credential 0xFFF2 service data payload into the struct: verify the service UUID
 * and extract flags, TX power, group/sub IDs, expiry, and tag.
 */
int ultrawidelock_ble_central_parse_adv(const uint8_t *svc_data, size_t len,
				struct ultrawidelock_ble_central_adv *out)
{
	if (svc_data == NULL || out == NULL || len != ULTRAWIDELOCK_BLE_CENTRAL_SVC_DATA_LEN) {
		return -1;
	}
	/* 0xFFF2 goes out little-endian (ultrawidelock_ble.c:567). */
	if (svc_data[0] != 0xF2u || svc_data[1] != 0xFFu) {
		return -1;
	}

	const uint8_t *p = svc_data + 2; /* payload bytes 0..23 */

	out->flags = p[0];
	out->tx_power = (int8_t)p[1];
	memcpy(out->group_id, p + 2, sizeof(out->group_id));
	memcpy(out->sub_id, p + 10, sizeof(out->sub_id));
	out->expiry = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) | ((uint32_t)p[14] << 8) |
		      (uint32_t)p[15];
	/* p[16] is reserved and carries no meaning; skip it. */
	memcpy(out->tag, p + 17, ULTRAWIDELOCK_ADVTAG_LEN);
	return 0;
}

/**
 * Return true if the advertisement group_id and sub_id fields match the first 8 and last 2 bytes of
 * the given 32-byte reader_id, false otherwise.
 */
int ultrawidelock_ble_central_adv_matches(const struct ultrawidelock_ble_central_adv *adv,
				  const uint8_t reader_id[32])
{
	if (adv == NULL || reader_id == NULL) {
		return 0;
	}
	return memcmp(adv->group_id, reader_id, 8) == 0 &&
	       memcmp(adv->sub_id, reader_id + 16, 2) == 0;
}

/**
 * Parse a BLE central read response payload containing SPSM, version list, and features; return 0
 * on success or -1 if the payload is malformed or truncated.
 */
int ultrawidelock_ble_central_parse_read_payload(const uint8_t *payload, size_t len,
					 struct ultrawidelock_ble_central_peer *out)
{
	if (payload == NULL || out == NULL || len < 3u) {
		return -1; /* SPSM (2) + at least the versions-length byte */
	}

	memset(out, 0, sizeof(*out));
	out->spsm = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);

	size_t vlen = payload[2];

	if ((vlen & 1u) != 0u || vlen > ULTRAWIDELOCK_BLE_CENTRAL_MAX_VERSIONS * 2u) {
		return -1; /* versions are 2 bytes each and the list is bounded */
	}
	if (len < 3u + vlen + 1u) {
		return -1; /* must still hold the features-length byte */
	}

	const uint8_t *v = payload + 3;

	out->versions_count = vlen / 2u;
	for (size_t i = 0; i < out->versions_count; i++) {
		out->versions[i] = (uint16_t)(((uint16_t)v[2u * i] << 8) | v[2u * i + 1u]);
	}

	const uint8_t *f = payload + 3u + vlen;
	size_t flen = f[0];

	/* The reader emits exactly one packed features byte; a peer that grows the
	 * field stays parseable as long as the buffer really holds it. */
	if (flen < 1u || len < (size_t)(f - payload) + 1u + flen) {
		return -1;
	}
	out->features = f[1];
	return 0;
}

/**
 * Serialize all BLE versions from the peer plus the selected version as big-endian 2-byte pairs
 * into the output buffer, and return 0 on success or -1 if output is too small or the peer is
 * invalid.
 */
int ultrawidelock_ble_central_blesk_salt(const struct ultrawidelock_ble_central_peer *peer,
					 uint16_t selected, uint8_t *out, size_t cap,
					 size_t *out_len)
{
	if (peer == NULL || out == NULL || out_len == NULL || peer->versions_count == 0u) {
		return -1;
	}

	size_t need = 2u * (peer->versions_count + 1u);

	if (cap < need) {
		return -1;
	}

	size_t n = 0;

	for (size_t i = 0; i < peer->versions_count; i++) {
		out[n++] = (uint8_t)(peer->versions[i] >> 8);
		out[n++] = (uint8_t)(peer->versions[i] & 0xffu);
	}
	out[n++] = (uint8_t)(selected >> 8);
	out[n++] = (uint8_t)(selected & 0xffu);
	*out_len = n;
	return 0;
}
