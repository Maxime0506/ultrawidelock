<!-- generated documentation — edit the source, not this file -->
# `firmware/src/prov_shell.c`

## API

### `static bool all_zero(const uint8_t *p, size_t len)`
`firmware/src/prov_shell.c:43`

Return true if all bytes in the buffer are zero.

**called by** `cmd_prov`, `dead_blob_reason`

### `static const char *dead_blob_reason(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)`
`firmware/src/prov_shell.c:57`

The three ways a syntactically valid blob is still useless, named rather than
left for the walk-up to discover. Same three tools/aliro_blob.py reports on a
flash dump, checked again here because a hex string can arrive by any route.
Returns a reason, or NULL when the blob will actually unlock.

**called by** `cmd_import`, `cmd_prov`  ·  **calls** `all_zero`

### `static int cmd_prov(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:79`

Shell command: display the reader's provisioning state — whether it is provisioned, its reader
ID, GRK status, enrolled anchors, and usability verdict.

**calls** `all_zero`, `dead_blob_reason`

### `static int cmd_import(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:113`

Deserialize a hex-encoded identity blob, reject it if syntactically valid but useless (no
identity, expired, or no trust anchors), then import it to settings storage if safe.

**calls** `dead_blob_reason`

### `static int cmd_export(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:160`

Serialize and print the reader identity and trust store as hex after confirming with "aliro
export yes"; the resulting string holds the private key and can impersonate the lock.

### `static int cmd_erase(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:193`

Erase the reader identity and all trust anchors after confirming with "aliro erase yes",
returning to the DEV identity with no anchors.

### `static int cmd_heap(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:217`

Run this straight after an `import`: the commit path does a software P-256
derive, which is the reader's heaviest single crypto step, and the peak is
cumulative since boot so one reading covers the whole command.
