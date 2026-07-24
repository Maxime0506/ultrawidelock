/* logfake: minimal <zephyr/init.h>. SYS_INIT emits a linkable const pointer
 * (logfake_sys_init_<fn>) so the suite can invoke the static init function. */
#ifndef LOGFAKE_ZEPHYR_INIT_H
#define LOGFAKE_ZEPHYR_INIT_H

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define SYS_INIT(fn, level, prio) int (*const logfake_sys_init_##fn)(void) = (fn)

#endif /* LOGFAKE_ZEPHYR_INIT_H */
