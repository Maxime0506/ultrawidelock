/* SPDX-License-Identifier: ISC */

/*
 * Zephyr-shaped macros, not Zephyr code.
 *
 * Pinned Nordic sources are compiled unmodified, so they need these spellings.
 * They are reproduced from the pinned Zephyr tree and kept here, outside every
 * compat layer's zephyr/ directory, so that no port source line includes a
 * Zephyr path and the BLE and Thread layers share one definition.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_VENDOR_MACROS_H
#define ULTRAWIDELOCK_FREERTOS_VENDOR_MACROS_H

#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define BIT_MASK(n) (BIT(n) - 1UL)

/*
 * Expands to 1 when the argument is defined as 1 and to 0 when it is defined
 * as anything else or left undefined, without tripping -Wundef.
 */
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val

#endif /* ULTRAWIDELOCK_FREERTOS_VENDOR_MACROS_H */
