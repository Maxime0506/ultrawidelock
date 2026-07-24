/* logfake: minimal <zephyr/shell/shell.h> for host-building aliro_shell.c.
 *
 * shell_print lands in the shellfake_out capture buffer (drvfake.c), and the
 * SHELL_* registration macros emit a plain table (struct shellfake_root) the
 * suite dispatches through — so the tests exercise the real command handlers,
 * not a real console. Driver-test binary only; the main host binary never
 * includes this header. */
#ifndef LOGFAKE_ZEPHYR_SHELL_SHELL_H
#define LOGFAKE_ZEPHYR_SHELL_SHELL_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include "drvfake.h" /* struct shell, shellfake_entry/root, shellfake_print */

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

#define shell_print(sh, ...) shellfake_print((sh), __VA_ARGS__)

#define SHELL_CMD(sym, subcmd, help, handler) {#sym, (handler)}
#define SHELL_SUBCMD_SET_END                  {NULL, NULL}
#define SHELL_STATIC_SUBCMD_SET_CREATE(name, ...)                                                  \
	static const struct shellfake_entry name[] = {__VA_ARGS__}
#define SHELL_CMD_REGISTER(name, sub, help, handler)                                               \
	const struct shellfake_root shellfake_cmd_##name = {                                       \
		#name, (const struct shellfake_entry *)(sub), (handler)}

#endif /* LOGFAKE_ZEPHYR_SHELL_SHELL_H */
