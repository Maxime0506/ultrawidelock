<!-- generated documentation — edit the source, not this file -->
# `firmware/src/aliro_prov_settings.c`

## API

### `static int prov_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)`
`firmware/src/aliro_prov_settings.c:29`

Settings callback to deserialize and store a provisioning blob read from persistent storage.
Validates that the blob does not exceed s_blob size and returns -EINVAL on overflow or read
error; on success stores the blob length and returns 0.

### `int aliro_prov_load(struct aliro_reader_identity *id, struct aliro_trust_store *ts)`
`firmware/src/aliro_prov_settings.c:54`

Load Aliro reader identity and trust anchors from persistent settings. Returns 0 on success with
stored data loaded, 1 if never provisioned (DEV identity used), -1 on any error (settings init,
load, or malformed blob; DEV identity used). On any failure the loaded identity defaults to DEV
and the error is logged as a warning.

### `int aliro_prov_erase(void)`
`firmware/src/aliro_prov_settings.c:98`

Erase the stored Aliro provisioning blob from persistent settings. Returns 0 on success, negative
on settings error; the error is logged as a warning and returned rather than suppressed, because
a silent factory reset that left the old anchors in place would pair but then reject the phone.

### `int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)`
`firmware/src/aliro_prov_settings.c:120`

Serialize and store Aliro reader identity and trust anchors to persistent settings. Uses a static
blob buffer to avoid stack overflow; safe because provisioning writes are rare and serialized on
s_prov_lock or the Matter work queue. Returns the result of settings_save_one.
