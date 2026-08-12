/*
 * The board's unrecoverable-failure hook.
 *
 * Reset rather than halt, because this is a door lock: a board spinning in a
 * fault is a lock that has stopped answering, while a board that reboots comes
 * back and can be opened. The reason is logged first, and the reset shows up in
 * RESETREAS as SOFTWARE, which is what otPlatGetResetReason reports afterwards.
 *
 * Define ULTRAWIDELOCK_FREERTOS_FATAL_HALT for bench builds to stop instead. The header
 * calls this hook "stop or reset" and both are in contract; which one is right
 * depends on whether a person is watching.
 */
#include <nrfx.h>

#include <ultrawidelock_freertos_platform.h>

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, "fatal", "%s",
				   reason != NULL ? reason : "(none)");

	__disable_irq();

#ifdef ULTRAWIDELOCK_FREERTOS_FATAL_HALT
	for (;;) {
		/* Hold the state for a debugger. */
	}
#else
	/*
	 * The barriers are the architecture's requirement for a reset request,
	 * not decoration: without them the write can still be in the store
	 * buffer when the core continues past it.
	 */
	__DSB();
	NVIC_SystemReset();
	for (;;) {
		/* NVIC_SystemReset does not return; this satisfies _Noreturn. */
	}
#endif
}
