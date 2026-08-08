/*
 * matter_fab_settings — the operational identity, across a reboot.
 *
 * woz_matter carries no storage seam on purpose (it is host-tested and has no
 * Zephyr in it), so the port owns this. Without it every reset silently
 * un-commissions the node: the fabric table is plain RAM, the node comes back
 * advertising commissionable, Thread never starts because nothing replays the
 * dataset, and the Home app shows an accessory that is simply gone.
 */
#ifndef MATTER_FAB_SETTINGS_H
#define MATTER_FAB_SETTINGS_H

#include "matter_clusters.h"

/**
 * Write the operational identity to the settings store.
 *
 * Call after anything that changes the fabric table or the Thread dataset.
 * Cheap enough to call on every such event and far cheaper than being wrong:
 * the alternative is a node that looks commissioned until it reboots.
 *
 * @return 0, or a negative errno from the settings backend.
 */
int matter_fab_store(const struct matter_device_info *info);

/**
 * Read it back into @p info.
 *
 * @return 1 when nothing was stored (never commissioned), 0 when a fabric was
 *         restored, negative on a settings error. A stored record written by a
 *         different firmware layout is DISCARDED rather than trusted -- see the
 *         note on the size check in the .c file.
 */
int matter_fab_load(struct matter_device_info *info);

/** Forget it, so the next boot comes up commissionable. */
int matter_fab_erase(void);

#endif /* MATTER_FAB_SETTINGS_H */
