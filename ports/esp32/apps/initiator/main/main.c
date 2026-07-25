// ESP32-S3 application entry for the Aliro initiator, the User-Device role that
// stands in for an iPhone on the bench. Stage 1a wires the BLE transport only: it
// starts the NimBLE central, which scans for the reader's 0xFFF2 advert, connects,
// reads the reader's SPSM, supported versions and features, writes the version it
// selects, and opens the L2CAP channel. It then reports what it learned, including
// the BleSK salt those versions imply, and dumps whatever the reader sends. It
// stops before AUTH0, because running the transaction needs an Access Credential
// the reader trusts and both ends must be provisioned out of band first.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Aliro initiator (User-Device role) — Stage 1a: BLE transport only.
 *
 * Scans for our reader's 0xFFF2 advert, connects, discovers the Aliro service,
 * reads the reader's SPSM/supported-versions/features, writes the version we
 * select, and opens the L2CAP CoC. It then reports what it learned and dumps any
 * SDU the reader sends; it does NOT yet run the Aliro transaction. That wiring
 * (aliro_device_on_command over this channel) needs the initiator to hold
 * credentials the reader trusts, which is the next slice.
 *
 * What this app is for: proving the transport half of the bench rig on silicon,
 * with no iPhone, before any protocol rides on it.
 */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aliro_ble_central.h"

static const char *TAG = "initiator";

/* Aliro protocol v1.0 — the only version our reader offers or we speak. */
#define INITIATOR_VERSION 0x0100u

/* The reader we are provisioned against. All zeros means "not provisioned yet":
 * the transport latches onto the first Aliro reader it sees and logs its group
 * id, which is how you discover the value to put here on a fresh bench. The full
 * 32-byte reader identity cannot come from the advert — that carries only
 * reader_id[0..7] and [16..17] — so it has to be copied from the reader's own
 * console output once, exactly as a phone is provisioned out of band. */
static const uint8_t k_reader_id[32] = {0};

static void on_ready(uint16_t conn_handle, const struct aliro_ble_central_peer *peer)
{
	ESP_LOGI(TAG, "=== transport up (conn %u) ===", conn_handle);
	ESP_LOGI(TAG, "  SPSM     0x%04x", (unsigned)peer->spsm);
	ESP_LOGI(TAG, "  features 0x%02x", peer->features);
	for (size_t i = 0; i < peer->versions_count; i++) {
		ESP_LOGI(TAG, "  version[%u] 0x%04x", (unsigned)i, (unsigned)peer->versions[i]);
	}

	/* The version list is the BleSK salt input (§11.8.1). Logging it is all we can
	 * do until this app owns a struct aliro_device, which needs a credential; the
	 * next slice hands these exact bytes to aliro_device_set_blesk_salt before
	 * AUTH1. Do not let it fall back to the v1.0 default: the nRF reader publishes
	 * two versions, so the default derives a BleSK it does not share. */
	uint8_t salt[2u * (ALIRO_BLE_CENTRAL_MAX_VERSIONS + 1u)];
	size_t salt_len = 0;

	if (aliro_ble_central_blesk_salt(peer, INITIATOR_VERSION, salt, sizeof(salt), &salt_len) ==
	    0) {
		char hex[2u * sizeof(salt) + 1u];

		for (size_t i = 0; i < salt_len; i++) {
			snprintf(hex + 2u * i, 3u, "%02x", salt[i]);
		}
		ESP_LOGI(TAG, "  BleSK salt %s", hex);
	}
	ESP_LOGI(TAG, "Stage 1a stops here: no credentials, so no AUTH0 yet.");
}

static void on_data(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	ESP_LOGI(TAG, "SDU from reader (conn %u, %u B): proto=0x%02x id=0x%02x", conn_handle,
		 (unsigned)len, len > 0 ? data[0] : 0, len > 1 ? data[1] : 0);
	ESP_LOG_BUFFER_HEX(TAG, data, len);
}

static void on_closed(uint16_t conn_handle)
{
	ESP_LOGW(TAG, "transport closed (conn %u)", conn_handle);
}

void app_main(void)
{
	struct aliro_ble_central_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	memcpy(cfg.reader_id, k_reader_id, sizeof(cfg.reader_id));
	cfg.selected_version = INITIATOR_VERSION;
	cfg.cb.on_ready = on_ready;
	cfg.cb.on_data = on_data;
	cfg.cb.on_closed = on_closed;

	if (aliro_ble_central_start(&cfg) != 0) {
		ESP_LOGE(TAG, "BLE central start failed");
		return;
	}
	ESP_LOGI(TAG, "Aliro initiator up; scanning for a reader");

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
