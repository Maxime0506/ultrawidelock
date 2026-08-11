/*
 * Zephyr's OpenThread integration header. The pinned radio platform includes
 * it unconditionally but names nothing from it: its instance handling comes
 * from OpenThread's own headers and its wake path from otSysEventSignalPending.
 * Checked by scripts/freertos-ncs-source-check.sh, which fails if the file ever
 * starts using it.
 */
#ifndef WOZ_OT_COMPAT_OPENTHREAD_H
#define WOZ_OT_COMPAT_OPENTHREAD_H

#endif /* WOZ_OT_COMPAT_OPENTHREAD_H */
