/** @file ultrawidelock_shell.h — the one seam the `ultrawidelock` console needs from the application. */

#ifndef ULTRAWIDELOCK_SHELL_H_
#define ULTRAWIDELOCK_SHELL_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supply the handler that `ultrawidelock factoryreset yes` invokes.
 *
 * A factory reset is a Matter/CHIP operation, and CHIP is C++. This file's
 * module is pure C and has no CHIP include paths, so the application registers
 * the call instead of this layer reaching up for it. Pass NULL to unregister.
 *
 * The handler is expected to erase provisioning and reboot; it may not return.
 * Left unregistered (host tests, a build without the application) the command
 * refuses with -ENOTSUP rather than pretending to have done anything.
 *
 * @param fn Reset handler, or NULL.
 */
void ultrawidelock_shell_set_factory_reset(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* ULTRAWIDELOCK_SHELL_H_ */
