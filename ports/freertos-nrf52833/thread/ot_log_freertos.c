/*
 * OpenThread's logging platform.
 *
 * One function, but it is the only way the stack says anything on a bench, and
 * the alternative to implementing it is compiling OpenThread's logging out --
 * which would make the first Thread bring-up on hardware silent about why it
 * failed. That trade is the wrong way round while the port has no hardware
 * verdict at all.
 *
 * The level mapping is deliberately lossy in one direction: OT_LOG_LEVEL_NOTE
 * and _INFO both arrive as INFO, because this port's logger has four levels and
 * OpenThread has six. Nothing downstream distinguishes them.
 */
#include <stdarg.h>

#include <ultrawidelock_freertos_platform.h>

#include <openthread/platform/logging.h>

#define OT_LOG_TAG "ot"

void otPlatLog(otLogLevel level, otLogRegion region, const char *format, ...)
{
	enum ultrawidelock_freertos_log_level mapped;
	va_list args;

	/* The region is the stack's own subsystem tag (MAC, MLE, ...). It is
	 * dropped rather than printed: OpenThread already spells the subsystem
	 * into the format string for every call site that cares. */
	(void)region;

	switch (level) {
	case OT_LOG_LEVEL_NONE:
		return;
	case OT_LOG_LEVEL_CRIT:
		mapped = ULTRAWIDELOCK_FREERTOS_LOG_ERROR;
		break;
	case OT_LOG_LEVEL_WARN:
		mapped = ULTRAWIDELOCK_FREERTOS_LOG_WARNING;
		break;
	case OT_LOG_LEVEL_NOTE:
	case OT_LOG_LEVEL_INFO:
		mapped = ULTRAWIDELOCK_FREERTOS_LOG_INFO;
		break;
	default:
		mapped = ULTRAWIDELOCK_FREERTOS_LOG_DEBUG;
		break;
	}

	va_start(args, format);
	ultrawidelock_freertos_log_va(mapped, OT_LOG_TAG, format, args);
	va_end(args);
}
