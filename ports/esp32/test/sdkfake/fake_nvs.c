/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * fake_nvs — in-RAM double of the ESP-IDF NVS blob API, one namespace ("*"),
 * one key, with per-call failure injection. Backs the aliro_prov_nvs.c and
 * aliro_ble.c host suites.
 */
#include <string.h>

#include "nvs_flash.h"

esp_err_t fake_nvs_init_rc = ESP_OK;
esp_err_t fake_nvs_init_rc_once = ESP_OK;
esp_err_t fake_nvs_open_rc = ESP_OK;
esp_err_t fake_nvs_get_rc = ESP_OK;
esp_err_t fake_nvs_set_rc = ESP_OK;
esp_err_t fake_nvs_commit_rc = ESP_OK;
int fake_nvs_erase_calls;

static uint8_t s_blob[1024];
static size_t s_blob_len;
static int s_have_blob;
static int s_have_namespace;

void fake_nvs_reset(void)
{
	fake_nvs_init_rc = ESP_OK;
	fake_nvs_init_rc_once = ESP_OK;
	fake_nvs_open_rc = ESP_OK;
	fake_nvs_get_rc = ESP_OK;
	fake_nvs_set_rc = ESP_OK;
	fake_nvs_commit_rc = ESP_OK;
	fake_nvs_erase_calls = 0;
	s_blob_len = 0;
	s_have_blob = 0;
	s_have_namespace = 0;
}

void fake_nvs_preload(const void *blob, size_t len)
{
	memcpy(s_blob, blob, len);
	s_blob_len = len;
	s_have_blob = 1;
	s_have_namespace = 1;
}

size_t fake_nvs_stored(void *out, size_t cap)
{
	size_t n = s_blob_len < cap ? s_blob_len : cap;

	if (s_have_blob) {
		memcpy(out, s_blob, n);
		return n;
	}
	return 0;
}

esp_err_t nvs_flash_init(void)
{
	if (fake_nvs_init_rc_once != ESP_OK) {
		esp_err_t rc = fake_nvs_init_rc_once;

		fake_nvs_init_rc_once = ESP_OK;
		return rc;
	}
	return fake_nvs_init_rc;
}

esp_err_t nvs_flash_erase(void)
{
	fake_nvs_erase_calls++;
	s_have_blob = 0;
	s_have_namespace = 0;
	s_blob_len = 0;
	return ESP_OK;
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
	(void)ns;
	if (fake_nvs_open_rc != ESP_OK) {
		return fake_nvs_open_rc;
	}
	if (mode == NVS_READONLY && !s_have_namespace) {
		return ESP_ERR_NVS_NOT_FOUND;
	}
	if (mode == NVS_READWRITE) {
		s_have_namespace = 1;
	}
	*out = 1u;
	return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
	(void)h;
	(void)key;
	if (fake_nvs_get_rc != ESP_OK) {
		return fake_nvs_get_rc;
	}
	if (!s_have_blob) {
		return ESP_ERR_NVS_NOT_FOUND;
	}
	if (*len < s_blob_len) {
		return ESP_ERR_NVS_INVALID_LENGTH;
	}
	memcpy(out, s_blob, s_blob_len);
	*len = s_blob_len;
	return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *val, size_t len)
{
	(void)h;
	(void)key;
	if (fake_nvs_set_rc != ESP_OK) {
		return fake_nvs_set_rc;
	}
	if (len > sizeof(s_blob)) {
		return ESP_FAIL;
	}
	memcpy(s_blob, val, len);
	s_blob_len = len;
	s_have_blob = 1;
	return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
	(void)h;
	return fake_nvs_commit_rc;
}

void nvs_close(nvs_handle_t h)
{
	(void)h;
}
