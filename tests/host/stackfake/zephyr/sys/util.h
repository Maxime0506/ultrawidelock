/* stackfake: <zephyr/sys/util.h>, the few macros the stack sources use. */
#ifndef STACKFAKE_ZEPHYR_SYS_UTIL_H
#define STACKFAKE_ZEPHYR_SYS_UTIL_H

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
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(cond, ...) static_assert((cond), "" __VA_ARGS__)
#endif

#endif /* STACKFAKE_ZEPHYR_SYS_UTIL_H */
