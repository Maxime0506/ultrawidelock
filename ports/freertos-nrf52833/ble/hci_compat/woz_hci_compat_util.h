/*
 * Zephyr-shaped macros, not Zephyr code.
 *
 * The pinned Nordic opcode dispatcher is compiled unmodified, so it needs
 * these three spellings. They are reproduced from the pinned Zephyr tree, and
 * they live outside the zephyr/ shim directory so that this port contains no
 * source line that includes a Zephyr path.
 */
#ifndef WOZ_HCI_COMPAT_UTIL_H
#define WOZ_HCI_COMPAT_UTIL_H

#define BIT(n) (1UL << (n))
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

#endif /* WOZ_HCI_COMPAT_UTIL_H */
