<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_fab_settings.c`

*No module docstring. First commit: "dwm3001cdk: keep the fabric table across a reboot".*

**depends on** [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md)

## API

### `int matter_fab_store(const struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:151`

Write the operational identity to the settings store.
Call after anything that changes the fabric table or the Thread dataset.
Cheap enough to call on every such event and far cheaper than being wrong:
the alternative is a node that looks commissioned until it reboots.
@return 0, or a negative errno from the settings backend.

### `int matter_fab_load(struct matter_device_info *info)`
`firmware/src/matter_fab_settings.c:210`

Read it back into @p info.
@return 1 when nothing was stored (never commissioned), 0 when a fabric was
restored, negative on a settings error. A stored record written by a
different firmware layout is DISCARDED rather than trusted -- see the
note on the size check in the .c file.

### `int matter_fab_erase(void)`
`firmware/src/matter_fab_settings.c:258`

Forget it, so the next boot comes up commissionable.

<details><summary>Undocumented (1)</summary>

- `fab_set`

</details>
