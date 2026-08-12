/*
 * Zephyr's assertions, routed to the port's fatal handler. They are kept live
 * rather than compiled out: every one of them in the radio platform guards a
 * driver contract that would otherwise fail silently much later.
 */
#ifndef ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_ASSERT_H
#define ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_ASSERT_H

_Noreturn void ultrawidelock_freertos_ot_assert_failed(const char *file, int line,
						       const char *test);

#define __ASSERT_NO_MSG(test)                                                                      \
	do {                                                                                       \
		if (!(test)) {                                                                     \
			ultrawidelock_freertos_ot_assert_failed(__FILE__, __LINE__, #test);                  \
		}                                                                                  \
	} while (0)

/* The message is a printf form Zephyr expands at the failure site only. */
#define __ASSERT(test, ...) __ASSERT_NO_MSG(test)

/* Zephyr evaluates the second form when assertions are on, which they are. */
#define __ASSERT_EVAL(expr1, expr2, test, ...)                                                     \
	do {                                                                                       \
		expr2;                                                                             \
		__ASSERT_NO_MSG(test);                                                             \
	} while (0)

#endif /* ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_ASSERT_H */
