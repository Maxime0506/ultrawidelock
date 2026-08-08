<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_fab_settings.c`

**depends on** [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md)

## API

### `static void fab_slots_str(const struct matter_device_info *info, char *out, size_t cap)`
`firmware/src/matter_fab_settings.c:84`

Render every fabric index as "1/2/0", for one log line.
Written out rather than printed as two arguments, because the two call sites
named fabrics[0] and fabrics[1] positionally and would have gone on reporting
exactly two however many the table holds -- a third fabric stored and
restored perfectly, with nothing in the log to say so.
@param out at least MATTER_SUPPORTED_FABRICS * 2 bytes.

**called by** `matter_fab_load`, `matter_fab_store`

### `static int fab_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)`
`firmware/src/matter_fab_settings.c:111`

Settings callback to deserialize and load stored Matter fabrics, Thread dataset, thread extended
PAN ID, ICAC metadata, and ICAC certificate. Validates version and layout match; rejects short
fabric reads and mismatched sizes to avoid loading truncated data. Returns -EINVAL on version or
layout mismatch, or the result of the read callback on success.

### `int matter_fab_store(const struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:224`

Write the operational identity to the settings store.
Call after anything that changes the fabric table or the Thread dataset.
Cheap enough to call on every such event and far cheaper than being wrong:
the alternative is a node that looks commissioned until it reboots.
@return 0, or a negative errno from the settings backend.

**calls** `fab_slots_str`

### `static void discard_partial(struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:331`

Undo a partial read. Shared, because two paths reject a record and both have
to leave the same nothing behind.

**called by** `matter_fab_load`

### `int matter_fab_load(struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:340`

Read it back into @p info.
@return 1 when nothing was stored (never commissioned), 0 when a fabric was
restored, negative on a settings error. A stored record written by a
different firmware layout is DISCARDED rather than trusted -- see the
note on the size check in the .c file.

**calls** `discard_partial`, `fab_slots_str`

### `int matter_fab_erase(void)`
`firmware/src/matter_fab_settings.c:405`

Forget it, so the next boot comes up commissionable.
