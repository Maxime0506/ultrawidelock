/* matterfake common_macros.h — esp-matter example helper. Failing the check
 * aborts, as the real macro's vTaskDelay-forever amounts to on target. */
#ifndef MATTERFAKE_COMMON_MACROS_H
#define MATTERFAKE_COMMON_MACROS_H

#include <stdlib.h>

#define ABORT_APP_ON_FAILURE(x, ...)                                           \
	do {                                                                   \
		if (!(x)) {                                                    \
			__VA_ARGS__;                                           \
			abort();                                               \
		}                                                              \
	} while (0)

#endif /* MATTERFAKE_COMMON_MACROS_H */
