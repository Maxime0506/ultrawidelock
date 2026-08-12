/*
 * ultrawidelock_util.h - container and compare macros.
 *
 * Pure code, same rationale as woz_bytes.h: none of this is platform-specific,
 * so it does not belong behind a <zephyr/sys/util.h> compat header.
 *
 * Every macro is #ifndef-guarded, so on Zephyr the real header wins if it was
 * already included and these are inert.
 */
#ifndef WOZ_UTIL_H
#define WOZ_UTIL_H

#include <stddef.h>

#if defined(__ZEPHYR__)
#include <zephyr/sys/util.h>
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
#ifndef CLAMP
#define CLAMP(x, lo, hi) MIN(MAX((x), (lo)), (hi))
#endif
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#ifndef ROUND_UP
#define ROUND_UP(x, align) (DIV_ROUND_UP((x), (align)) * (align))
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field) ((type *)(((char *)(ptr)) - offsetof(type, field)))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) ((void)(x))
#endif

/* Zephyr's IS_ENABLED: 1 when the arg is defined to 1, 0 when undefined. */
#ifndef IS_ENABLED
#define IS_ENABLED(config_macro)             Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro)          Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1                               _YYYY,
/* Token-splice: the bare arg is the mechanism. NOLINTNEXTLINE(bugprone-macro-parentheses) */
#define Z_IS_ENABLED2(one_or_two_args)       Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val
#endif

#endif /* WOZ_UTIL_H */
