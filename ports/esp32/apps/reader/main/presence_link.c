// Presence dongle protocol (see presence_link.h). Serves the host challenge on the
// console UART: latches the latest trusted range via the facade's range listener,
// and answers each nonce with an HMAC-signed assertion of the current presence
// state. The pairing key lives in NVS and can be (re)loaded in-band.
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
#include "presence_link.h"

#define PRESENCE_NS  "presence"
#define PRESENCE_KEY "kpair"
/* A range must have latched within this window for the dongle to call it PRESENT;
 * otherwise the phone has walked away and presence has gone stale. */
#define RANGE_FRESH_US (5 * 1000 * 1000)

static volatile int32_t s_last_cm;
static volatile int64_t s_last_us;
static volatile bool s_have_range;
static uint8_t s_key[ALIRO_ASSERT_KEY_LEN];
static bool s_key_set;

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

void presence_link_init(void)
{
	load_key();
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

// Assemble + sign the assertion for a challenge nonce and write it to the host.
static void answer_challenge(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	struct aliro_assert a;

	memset(&a, 0, sizeof(a));
	memcpy(a.nonce, nonce, ALIRO_ASSERT_NONCE_LEN);
	a.uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);

	int64_t now = esp_timer_get_time();
	uint8_t cred_pub[65];
	bool fresh = s_have_range && (now - s_last_us) <= RANGE_FRESH_US && s_last_cm >= 0;
	bool have_cred = aliro_reader_authenticated_credential(cred_pub);

	if (fresh && have_cred) {
		a.status = ALIRO_PRESENCE_PRESENT;
		a.distance_cm = (s_last_cm > 0xFFFE) ? 0xFFFEu : (uint16_t)s_last_cm;
		aliro_assert_cred_id(cred_pub, a.cred_id);
	} else {
		a.status = ALIRO_PRESENCE_ABSENT;
		a.distance_cm = ALIRO_ASSERT_DIST_NONE;
	}

	/* With no pairing key yet, sign under an all-zero key so the host's MAC check
	 * fails cleanly (a clean deny) instead of the dongle going silent. */
	static const uint8_t zero_key[ALIRO_ASSERT_KEY_LEN] = { 0 };
	const uint8_t *key = s_key_set ? s_key : zero_key;
	uint8_t wire[ALIRO_ASSERT_WIRE_LEN];

	aliro_assert_build(key, &a, wire, sizeof(wire), NULL);
	fwrite(wire, 1, sizeof(wire), stdout);
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
		if (t == 'C') { /* challenge: 16-byte nonce follows */
			uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];

			read_full(nonce, sizeof(nonce));
			answer_challenge(nonce);
		} else if (t == 'K') { /* key-load: 32-byte pairing key follows */
			uint8_t key[ALIRO_ASSERT_KEY_LEN];

			read_full(key, sizeof(key));
			(void)presence_link_set_key(key);
		}
		/* any other second byte: ignore, keep scanning */
	}
}
