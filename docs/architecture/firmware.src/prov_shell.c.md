<!-- generated documentation — edit the source, not this file -->
# `firmware/src/prov_shell.c`

## API

### `static const char *dead_blob_reason(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)`
`firmware/src/prov_shell.c:54`

The three ways a syntactically valid blob is still useless, named rather than
left for the walk-up to discover. Same three tools/aliro_blob.py reports on a
flash dump, checked again here because a hex string can arrive by any route.
Returns a reason, or NULL when the blob will actually unlock.

**called by** `cmd_import`, `cmd_prov`  ·  **calls** `all_zero`

### `static int cmd_heap(const struct shell *sh, size_t argc, char **argv)`
`firmware/src/prov_shell.c:198`

Run this straight after an `import`: the commit path does a software P-256
derive, which is the reader's heaviest single crypto step, and the peak is
cumulative since boot so one reading covers the whole command.

<details><summary>Undocumented (5)</summary>

- `all_zero`
- `cmd_prov`
- `cmd_import`
- `cmd_export`
- `cmd_erase`

</details>
