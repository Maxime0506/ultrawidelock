/* SPDX-License-Identifier: ISC */

/*
 * The handful of Zephyr utility macros the shared Matter sources reach for.
 *
 * Reached indirectly: apps/dwm3001cdk-lock/src/status_led.h includes this to
 * spell its signal bits, and matter_commission.c includes that header. A quoted
 * include resolves relative to the INCLUDING file first, so the Zephyr app's
 * status_led.h is the one found no matter what -I order this build uses.
 *
 * That is NOT automatically fine, although an earlier version of this comment
 * said it was: the declarations match the FreeRTOS product's, but they sit
 * behind IS_ENABLED(CONFIG_ULTRAWIDELOCK_STATUS_LED), and without that macro
 * every LED call in matter_commission.c compiles to the header's no-op stub --
 * no warning, no missing symbol, just a lamp that ignores the Door Lock
 * cluster. The port CMakeLists defines CONFIG_ULTRAWIDELOCK_STATUS_LED=1 for
 * exactly this reason; see the note beside ultrawidelock_matter_transport.
 */
#ifndef ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_SYS_UTIL_H
#define ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_SYS_UTIL_H

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif /* ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_SYS_UTIL_H */
