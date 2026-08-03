/* nfcfake: <zephyr/sys/util.h>. Guarded throughout — kernel.h defines the same
 * names and either header may be included first. */
#ifndef NFCFAKE_ZEPHYR_SYS_UTIL_H
#define NFCFAKE_ZEPHYR_SYS_UTIL_H

#include <stddef.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif
#ifndef ROUND_UP
#define ROUND_UP(x, align) ((((x) + ((align) - 1)) / (align)) * (align))
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field)                                                             \
	((type *)(void *)((char *)(ptr) - offsetof(type, field)))
#endif
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(cond, ...) _Static_assert((cond), "" __VA_ARGS__)
#endif

#endif /* NFCFAKE_ZEPHYR_SYS_UTIL_H */
