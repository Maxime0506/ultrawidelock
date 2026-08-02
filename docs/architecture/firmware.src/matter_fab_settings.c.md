<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_fab_settings.c`

**depends on** [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md)

## API

### `int matter_fab_store(const struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:185`

Write the operational identity to the settings store.
Call after anything that changes the fabric table or the Thread dataset.
Cheap enough to call on every such event and far cheaper than being wrong:
the alternative is a node that looks commissioned until it reboots.
@return 0, or a negative errno from the settings backend.

### `static void discard_partial(struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:283`

Undo a partial read. Shared, because two paths reject a record and both have
to leave the same nothing behind.

**called by** `matter_fab_load`

### `int matter_fab_load(struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:292`

Read it back into @p info.
@return 1 when nothing was stored (never commissioned), 0 when a fabric was
restored, negative on a settings error. A stored record written by a
different firmware layout is DISCARDED rather than trusted -- see the
note on the size check in the .c file.

**calls** `discard_partial`

### `int matter_fab_erase(void)`
`firmware/src/matter_fab_settings.c:353`

Forget it, so the next boot comes up commissionable.

<details><summary>Undocumented (1)</summary>

- `fab_set`

</details>
