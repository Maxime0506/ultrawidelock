/*
 * The handful of Zephyr utility macros the shared Matter sources reach for.
 *
 * Reached indirectly: apps/dwm3001cdk-lock/src/status_led.h includes this to
 * spell its signal bits, and matter_commission.c includes that header. A quoted
 * include resolves relative to the INCLUDING file first, so the Zephyr app's
 * status_led.h is the one found no matter what -I order this build uses -- and
 * that is fine, because the two products' headers declare the same functions
 * and the same enum with the same values. Only BIT() was missing.
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
