/* sdkfake nvs.h — in-RAM single-namespace blob store with failure injection.
 * Implemented in fake_nvs.c. */
#ifndef SDKFAKE_NVS_H
#define SDKFAKE_NVS_H

#include "esp_err.h"

typedef uint32_t nvs_handle_t;

typedef enum {
	NVS_READONLY,
	NVS_READWRITE,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *val, size_t len);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);

/* ---- test controls (fake_nvs.c) ---- */
extern esp_err_t fake_nvs_init_rc;      /* nvs_flash_init result (ESP_OK) */
extern esp_err_t fake_nvs_init_rc_once; /* one-shot init result, then init_rc */
extern esp_err_t fake_nvs_open_rc;      /* forced nvs_open result (ESP_OK = off) */
extern esp_err_t fake_nvs_get_rc;       /* forced nvs_get_blob result */
extern esp_err_t fake_nvs_set_rc;       /* forced nvs_set_blob result */
extern esp_err_t fake_nvs_commit_rc;    /* forced nvs_commit result */
extern int fake_nvs_erase_calls;        /* nvs_flash_erase() count */
void fake_nvs_reset(void);              /* wipe store + clear all knobs */
void fake_nvs_preload(const void *blob, size_t len); /* seed the stored blob */
size_t fake_nvs_stored(void *out, size_t cap);       /* copy out the stored blob */

#endif
