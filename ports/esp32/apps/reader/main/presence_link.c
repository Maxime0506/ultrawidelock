// Presence dongle protocol (see presence_link.h). Serves the host challenge on the
// console UART: latches the latest trusted range via the facade's range listener,
// and answers each nonce with a signed assertion of the current presence state.
// Two signing paths: an HMAC under the NVS pairing key, verifiable only by the
// paired host, and ECDSA-P256 under a per-device key generated on first boot,
// verifiable by anyone holding the public point.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"
#include "aliro_assert.h"
#include "aliro_assert_ec.h"
#include "aliro_prim.h"
#include "presence_link.h"

#define PRESENCE_NS     "presence"
#define PRESENCE_KEY    "kpair"
#define PRESENCE_DEVKEY "kdev"
/* A range must have latched within this window for the dongle to call it PRESENT;
 * otherwise the phone has walked away and presence has gone stale. */
#define RANGE_FRESH_US (5 * 1000 * 1000)

static volatile int32_t s_last_cm;
static volatile int64_t s_last_us;
static volatile bool s_have_range;
static uint8_t s_key[ALIRO_ASSERT_KEY_LEN];
static bool s_key_set;

/* Device signing identity. Only the private scalar is persisted; the public point
 * is re-derived at every boot so the two can never drift apart in NVS. */
static struct aliro_assert_ec_priv s_dev;
static uint8_t s_dev_pub[ALIRO_ASSERT_PUB_LEN];
static bool s_dev_set;

// Range-latch listener (runs on the UWB RX path): record the latest trusted range
// + the time it landed. Keep it to a couple of stores, nothing heavier.
static void range_cb(void)
{
	int32_t cm;

	if (woz_uwb_trusted_range_cm(&cm)) {
		s_last_cm = cm;
		s_last_us = esp_timer_get_time();
		s_have_range = true;
	}
}

static void load_key(void)
{
	nvs_handle_t h;

	if (nvs_open(PRESENCE_NS, NVS_READONLY, &h) != ESP_OK) {
		return;
	}
	size_t len = sizeof(s_key);
	if (nvs_get_blob(h, PRESENCE_KEY, s_key, &len) == ESP_OK && len == sizeof(s_key)) {
		s_key_set = true;
	}
	nvs_close(h);
}

int presence_link_set_key(const uint8_t key[32])
{
	nvs_handle_t h;

	if (nvs_open(PRESENCE_NS, NVS_READWRITE, &h) != ESP_OK) {
		return -1;
	}
	esp_err_t e = nvs_set_blob(h, PRESENCE_KEY, key, ALIRO_ASSERT_KEY_LEN);

	if (e == ESP_OK) {
		e = nvs_commit(h);
	}
	nvs_close(h);
	if (e != ESP_OK) {
		return -1;
	}
	memcpy(s_key, key, ALIRO_ASSERT_KEY_LEN);
	s_key_set = true;
	return 0;
}

// Load the device signing key from NVS, generating and persisting one on first
// boot. This key IS the dongle's identity to every third-party verifier, so it has
// to outlive reboots: a key regenerated each boot would silently invalidate every
// public key anyone had already enrolled.
static void load_or_make_dev_key(void)
{
	nvs_handle_t h;
	uint8_t priv[ALIRO_ASSERT_KEY_LEN];
	size_t len = sizeof(priv);
	bool have = false;

	if (nvs_open(PRESENCE_NS, NVS_READONLY, &h) == ESP_OK) {
		have = nvs_get_blob(h, PRESENCE_DEVKEY, priv, &len) == ESP_OK &&
		       len == sizeof(priv);
		nvs_close(h);
	}

	if (have) {
		if (aliro_ec_p256_pub_from_priv(priv, s_dev_pub) != 0) {
			return;
		}
	} else {
		if (aliro_ec_p256_keygen(priv, s_dev_pub) != 0) {
			return;
		}
		if (nvs_open(PRESENCE_NS, NVS_READWRITE, &h) != ESP_OK) {
			return;
		}
		esp_err_t e = nvs_set_blob(h, PRESENCE_DEVKEY, priv, sizeof(priv));

		if (e == ESP_OK) {
			e = nvs_commit(h);
		}
		nvs_close(h);
		/* Refuse to sign under a key that did not reach flash. Handing out a
		 * public key the dongle forgets at the next reboot is worse than
		 * having no key at all: enrolment would appear to succeed. */
		if (e != ESP_OK) {
			memset(s_dev_pub, 0, sizeof(s_dev_pub));
			return;
		}
	}

	memcpy(s_dev.d, priv, sizeof(priv));
	s_dev_set = true;
}

void presence_link_init(void)
{
	/* Idempotent (psa_crypto_init is), and aliro_reader_start() has already run
	 * it. Repeated here so the keygen below does not depend on that ordering. */
	(void)aliro_prim_init();
	load_key();
	load_or_make_dev_key();
	woz_uwb_set_range_listener(range_cb);
}

// Blocking read of exactly n bytes from stdin, yielding while the UART is idle.
static void read_full(uint8_t *b, size_t n)
{
	size_t got = 0;

	while (got < n) {
		int c = getchar();

		if (c == EOF) {
			vTaskDelay(pdMS_TO_TICKS(2));
			continue;
		}
		b[got++] = (uint8_t)c;
	}
}

// Fill in the assertion body both signing paths share: the challenge nonce plus
// the current presence verdict from the range latch and the authenticated
// credential. Signing is the caller's job, so the two modes state identical facts.
static void fill_assert(struct aliro_assert *a, const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	memset(a, 0, sizeof(*a));
	memcpy(a->nonce, nonce, ALIRO_ASSERT_NONCE_LEN);
	a->uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
	/* unix_ms stays ALIRO_ASSERT_TIME_NONE (the memset above): this dongle has
	 * no trusted wall clock, and claiming one it cannot back would be worse
	 * than admitting it has none. Freshness therefore rests entirely on the
	 * verifier's nonce being unpredictable -- which holds for a third-party
	 * verifier of a P-256 frame exactly as it does for the paired host. */

	int64_t now = esp_timer_get_time();
	uint8_t cred_pub[65];
	bool fresh = s_have_range && (now - s_last_us) <= RANGE_FRESH_US && s_last_cm >= 0;
	bool have_cred = aliro_reader_authenticated_credential(cred_pub);

	if (fresh && have_cred) {
		a->status = ALIRO_PRESENCE_PRESENT;
		a->distance_cm = (s_last_cm > 0xFFFE) ? 0xFFFEu : (uint16_t)s_last_cm;
		aliro_assert_cred_id(cred_pub, a->cred_id);
	} else {
		a->status = ALIRO_PRESENCE_ABSENT;
		a->distance_cm = ALIRO_ASSERT_DIST_NONE;
	}
}

// Assemble + HMAC the assertion for a challenge nonce and write it to the host.
// Only the paired host, holding the same secret, can check this frame.
static void answer_challenge(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	struct aliro_assert a;

	fill_assert(&a, nonce);

	/* With no pairing key yet, sign under an all-zero key so the host's MAC check
	 * fails cleanly (a clean deny) instead of the dongle going silent. */
	static const uint8_t zero_key[ALIRO_ASSERT_KEY_LEN] = { 0 };
	const uint8_t *key = s_key_set ? s_key : zero_key;
	uint8_t wire[ALIRO_ASSERT_WIRE_HMAC];

	aliro_assert_build(key, &a, wire, sizeof(wire), NULL);
	fwrite(wire, 1, sizeof(wire), stdout);
	fflush(stdout);
}

// As above but signed under the device key, so any holder of the public point can
// verify it without sharing a secret. This is what makes a presence proof portable
// to a third party (a CI job, a second reviewer) rather than only to this host.
static void answer_challenge_p256(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	struct aliro_assert a;
	uint8_t wire[ALIRO_ASSERT_WIRE_P256];

	fill_assert(&a, nonce);

	if (!s_dev_set || aliro_assert_build_p256(aliro_assert_ec_sign, &s_dev, &a, wire,
						  sizeof(wire), NULL) != 0) {
		/* No usable device key, or the signature failed. Answer with an
		 * all-zero frame rather than going silent: the host then fails the
		 * framing check and denies, instead of blocking on a read of a
		 * response that never arrives. */
		memset(wire, 0, sizeof(wire));
	}
	fwrite(wire, 1, sizeof(wire), stdout);
	fflush(stdout);
}

// Answer a public-key request with the device's uncompressed P-256 point, which is
// how a host enrols this dongle. All-zero when there is no key, and no verifier
// accepts a frame under that point.
static void send_pubkey(void)
{
	fwrite(s_dev_pub, 1, sizeof(s_dev_pub), stdout);
	fflush(stdout);
}

void presence_link_serve(void)
{
	/* Mute logging so it cannot corrupt the binary channel, and make stdio
	 * unbuffered so frames go out whole and immediately. */
	esp_log_level_set("*", ESP_LOG_NONE);
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	for (;;) {
		int c = getchar();

		if (c == EOF) {
			vTaskDelay(pdMS_TO_TICKS(5));
			continue;
		}
		if (c != 'A') {
			continue; /* resync on the frame lead byte */
		}
		int t = getchar();
		while (t == EOF) {
			vTaskDelay(pdMS_TO_TICKS(2));
			t = getchar();
		}
		if (t == 'C') { /* HMAC challenge: 16-byte nonce follows */
			uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];

			read_full(nonce, sizeof(nonce));
			answer_challenge(nonce);
		} else if (t == 'P') { /* P-256 challenge: 16-byte nonce follows */
			uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];

			read_full(nonce, sizeof(nonce));
			answer_challenge_p256(nonce);
		} else if (t == 'Q') { /* public-key request: no payload */
			send_pubkey();
		} else if (t == 'K') { /* key-load: 32-byte pairing key follows */
			uint8_t key[ALIRO_ASSERT_KEY_LEN];

			read_full(key, sizeof(key));
			(void)presence_link_set_key(key);
		}
		/* any other second byte: ignore, keep scanning */
	}
}
