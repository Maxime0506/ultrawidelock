/*
 * Host tests for the device-side BLE transport decoders (ultrawidelock_ble_central.c):
 * the 0xFFF2 advert, the reader-SPSM READ payload, and the BleSK salt built from
 * the version list that READ carries.
 *
 * Anti-self-consistency: the fixtures here are assembled field-for-field the way
 * the SHIPPED reader emits them (build_ultrawidelock_svc_data and build_read_payload in
 * ports/esp32/components/ultrawidelock_ble/ultrawidelock_ble.c), and the salt check is pinned to
 * the reader's own hardcoded BleSK salt — the 01 00 01 00 that ultrawidelock_reader.c
 * builds from k_proto_versions — rather than to anything this file computes.
 * That is what makes the multi-version path meaningful instead of circular.
 */
#include <stdio.h>
#include <string.h>

#include "ultrawidelock_ble_central.h"

static int fails;

static void t_ok_(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

#define T_OK(n, c) t_ok_((n), (c) ? 1 : 0)

/* Assemble 0xFFF2 service data exactly as build_ultrawidelock_svc_data does. */
static void make_adv(uint8_t out[ULTRAWIDELOCK_BLE_CENTRAL_SVC_DATA_LEN],
		     const uint8_t reader_id[32], uint32_t expiry)
{
	uint8_t *p = out;

	*p++ = 0xF2u; /* UUID little-endian */
	*p++ = 0xFFu;
	*p++ = 0x80u;               /* flags: BLE+UWB supported, advert version 0 */
	*p++ = (uint8_t)(int8_t)-4; /* tx power */
	memcpy(p, reader_id, 8);    /* truncated group id */
	p += 8;
	memcpy(p, reader_id + 16, 2); /* truncated group sub id */
	p += 2;
	*p++ = (uint8_t)(expiry >> 24);
	*p++ = (uint8_t)(expiry >> 16);
	*p++ = (uint8_t)(expiry >> 8);
	*p++ = (uint8_t)expiry;
	*p++ = 0x00u; /* reserved */
	for (unsigned i = 0; i < ULTRAWIDELOCK_ADVTAG_LEN; i++) {
		*p++ = (uint8_t)(0xA0u + i); /* dynamic tag (opaque here) */
	}
}

/* Assemble a reader-SPSM READ payload exactly as build_read_payload does. */
static size_t make_read_payload(uint8_t *out, uint16_t spsm, const uint16_t *versions,
				size_t nversions, uint8_t features)
{
	uint8_t *p = out;

	*p++ = (uint8_t)(spsm >> 8);
	*p++ = (uint8_t)(spsm & 0xffu);
	*p++ = (uint8_t)(nversions * 2u);
	for (size_t i = 0; i < nversions; i++) {
		*p++ = (uint8_t)(versions[i] >> 8);
		*p++ = (uint8_t)(versions[i] & 0xffu);
	}
	*p++ = 1u; /* features length */
	*p++ = features;
	return (size_t)(p - out);
}

static void test_adv(void)
{
	printf("\n== 0xFFF2 advert decode ==\n");

	uint8_t reader_id[32];

	for (int i = 0; i < 32; i++) {
		reader_id[i] = (uint8_t)(0x30 + i);
	}

	uint8_t svc[ULTRAWIDELOCK_BLE_CENTRAL_SVC_DATA_LEN];
	struct ultrawidelock_ble_central_adv adv;

	make_adv(svc, reader_id, 0x6890ABCDu);
	T_OK("adv.parse", ultrawidelock_ble_central_parse_adv(svc, sizeof(svc), &adv) == 0);
	T_OK("adv.flags", adv.flags == 0x80u);
	T_OK("adv.txpower", adv.tx_power == -4);
	T_OK("adv.groupid==reader_id[0..7]", memcmp(adv.group_id, reader_id, 8) == 0);
	T_OK("adv.subid==reader_id[16..17]", memcmp(adv.sub_id, reader_id + 16, 2) == 0);
	T_OK("adv.expiry", adv.expiry == 0x6890ABCDu);
	T_OK("adv.tag[0]", adv.tag[0] == 0xA0u && adv.tag[ULTRAWIDELOCK_ADVTAG_LEN - 1u] == 0xA6u);

	T_OK("adv.matches-our-reader", ultrawidelock_ble_central_adv_matches(&adv, reader_id) == 1);

	/* a different reader must not match: flip one byte inside the truncation */
	uint8_t other[32];

	memcpy(other, reader_id, 32);
	other[3] ^= 0x01u;
	T_OK("adv.rejects-other-reader", ultrawidelock_ble_central_adv_matches(&adv, other) == 0);

	/* a byte outside both truncated spans is invisible to the match, by design */
	memcpy(other, reader_id, 32);
	other[20] ^= 0x01u;
	T_OK("adv.match-ignores-untruncated-bytes",
	     ultrawidelock_ble_central_adv_matches(&adv, other) == 1);

	/* no-clock readers advertise the sentinel expiry */
	make_adv(svc, reader_id, 0xFFFFFFFFu);
	T_OK("adv.expiry-unavailable",
	     ultrawidelock_ble_central_parse_adv(svc, sizeof(svc), &adv) == 0 &&
		     adv.expiry == 0xFFFFFFFFu);

	printf("\n== advert decode rejects malformed input ==\n");
	T_OK("adv.short", ultrawidelock_ble_central_parse_adv(svc, sizeof(svc) - 1u, &adv) == -1);
	T_OK("adv.long", ultrawidelock_ble_central_parse_adv(svc, sizeof(svc) + 1u, &adv) == -1);
	svc[0] = 0xF1u; /* not the Aliro service */
	T_OK("adv.wrong-uuid", ultrawidelock_ble_central_parse_adv(svc, sizeof(svc), &adv) == -1);
}

static void test_read_payload(void)
{
	printf("\n== reader-SPSM READ payload decode ==\n");

	uint8_t buf[64];
	struct ultrawidelock_ble_central_peer peer;

	/* what our reader actually publishes today: SPSM 0x0080, versions {0x0100} */
	const uint16_t v1[] = {0x0100u};
	size_t n = make_read_payload(buf, 0x0080u, v1, 1u, 0x05u);

	T_OK("read.parse", ultrawidelock_ble_central_parse_read_payload(buf, n, &peer) == 0);
	T_OK("read.spsm==0x0080", peer.spsm == 0x0080u);
	T_OK("read.versions", peer.versions_count == 1u && peer.versions[0] == 0x0100u);
	T_OK("read.features", peer.features == 0x05u);

	printf("\n== READ decode rejects malformed input ==\n");
	T_OK("read.short", ultrawidelock_ble_central_parse_read_payload(buf, 2u, &peer) == -1);
	T_OK("read.truncated-features",
	     ultrawidelock_ble_central_parse_read_payload(buf, n - 1u, &peer) == -1);

	uint8_t bad[64];

	memcpy(bad, buf, n);
	bad[2] = 3u; /* odd versions length: versions are 2 bytes each */
	T_OK("read.odd-versions-len", ultrawidelock_ble_central_parse_read_payload(bad, n, &peer) == -1);

	memcpy(bad, buf, n);
	bad[2] = (uint8_t)((ULTRAWIDELOCK_BLE_CENTRAL_MAX_VERSIONS + 1u) * 2u);
	T_OK("read.too-many-versions", ultrawidelock_ble_central_parse_read_payload(bad, n, &peer) == -1);

	memcpy(bad, buf, n);
	bad[2] = 200u; /* versions length overruns the buffer */
	T_OK("read.versions-len-overrun",
	     ultrawidelock_ble_central_parse_read_payload(bad, n, &peer) == -1);

	memcpy(bad, buf, n);
	bad[n - 2u] = 0u; /* features length of zero */
	T_OK("read.zero-features-len", ultrawidelock_ble_central_parse_read_payload(bad, n, &peer) == -1);
}

static void test_blesk_salt(void)
{
	printf("\n== BleSK salt from the peer's published version list ==\n");

	uint8_t buf[64], salt[32];
	struct ultrawidelock_ble_central_peer peer;
	size_t n, sl = 0;

	/* THE anchor: for our v1.0-only reader the salt derived from the GATT list
	 * must be byte-identical to the 01 00 01 00 that ultrawidelock_reader.c builds from
	 * k_proto_versions, which is what the already-proven ultrawidelock_dev_blesk_init
	 * call sites hardcode. */
	static const uint8_t k_reader_salt_v1[] = {0x01, 0x00, 0x01, 0x00};
	const uint16_t v1[] = {0x0100u};

	n = make_read_payload(buf, 0x0080u, v1, 1u, 0x05u);
	T_OK("salt.parse", ultrawidelock_ble_central_parse_read_payload(buf, n, &peer) == 0);
	T_OK("salt.build",
	     ultrawidelock_ble_central_blesk_salt(&peer, 0x0100u, salt, sizeof(salt), &sl) == 0);
	T_OK("salt==reader's 01 00 01 00",
	     sl == sizeof(k_reader_salt_v1) && memcmp(salt, k_reader_salt_v1, sl) == 0);

	/* a multi-version peer must produce a LONGER, DIFFERENT salt — proof the
	 * value is read from the peer rather than hardcoded */
	const uint16_t v2[] = {0x0100u, 0x0200u};

	n = make_read_payload(buf, 0x0080u, v2, 2u, 0x05u);
	T_OK("salt.parse-multi", ultrawidelock_ble_central_parse_read_payload(buf, n, &peer) == 0);
	T_OK("salt.build-multi",
	     ultrawidelock_ble_central_blesk_salt(&peer, 0x0200u, salt, sizeof(salt), &sl) == 0);

	static const uint8_t k_expect_multi[] = {0x01, 0x00, 0x02, 0x00, 0x02, 0x00};

	T_OK("salt.multi==versions||selected",
	     sl == sizeof(k_expect_multi) && memcmp(salt, k_expect_multi, sl) == 0);
	T_OK("salt.multi differs from v1-only salt",
	     !(sl == sizeof(k_reader_salt_v1) && memcmp(salt, k_reader_salt_v1, 4) == 0));

	printf("\n== salt rejects bad input ==\n");
	T_OK("salt.cap-too-small",
	     ultrawidelock_ble_central_blesk_salt(&peer, 0x0100u, salt, 5u, &sl) == -1);

	struct ultrawidelock_ble_central_peer empty;

	memset(&empty, 0, sizeof(empty));
	T_OK("salt.no-versions",
	     ultrawidelock_ble_central_blesk_salt(&empty, 0x0100u, salt, sizeof(salt), &sl) == -1);
}

int main(void)
{
	printf("== ultrawidelock_ble_central: device-side BLE transport decoders ==\n");
	test_adv();
	test_read_payload();
	test_blesk_salt();

	if (fails) {
		printf("\nRESULT: %d FAIL\n", fails);
		return 1;
	}
	printf("\nRESULT: PASS\n");
	return 0;
}
