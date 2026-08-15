/* SPDX-License-Identifier: ISC */

/** @file ultrawidelock_diag.h — DIAGK(): gate for verbose UWB bring-up diagnostics. */

#ifndef ULTRAWIDELOCK_DIAG_H
#define ULTRAWIDELOCK_DIAG_H

#include "ultrawidelock_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime gate for the per-frame trace (defined in ccc_shim_rx.c). On ESP-IDF
 * it defaults off: DIAGK is synchronous printf from inside the UWB task, and at
 * 115200 baud the trace saturates the console against ~1.8 ms ranging-slot
 * deadlines (bench-correlated with late RESPONSE arms). Re-enable at runtime
 * with the app shell's `uwbdiag on`. Zephyr and host builds keep the always-on
 * default (fast RTT/stdout console, unchanged behavior). */
extern volatile int ultrawidelock_uwb_diag_on;

#ifdef __cplusplus
}
#endif

#if defined(ESP_PLATFORM)
#define ULTRAWIDELOCK_UWB_DIAG_DEFAULT 0
#else
#define ULTRAWIDELOCK_UWB_DIAG_DEFAULT 1
#endif

#if defined(CONFIG_ULTRAWIDELOCK_PRETTY_SHELL)
#define DIAGK(...)                                                                                 \
	do {                                                                                       \
		if (0) {                                                                           \
			ultrawidelock_printf(__VA_ARGS__);                                                   \
		}                                                                                  \
	} while (0)
#else
#define DIAGK(...)                                                                                 \
	do {                                                                                       \
		if (ultrawidelock_uwb_diag_on) {                                                             \
			ultrawidelock_printf(__VA_ARGS__);                                                   \
		}                                                                                  \
	} while (0)
#endif

#endif /* ULTRAWIDELOCK_DIAG_H */
