/*
 * Where nrfx driver logging goes on this port: nowhere.
 *
 * nrfx expects the integration to supply this header, and nrfx ships a template
 * that discards everything. This is that template's content, restated here for
 * one specific reason rather than reached by putting nrfx/templates on the
 * include path: that directory ALSO contains an nrfx_glue.h, and adding it
 * would silently shadow board/nrfx_config/nrfx_glue.h -- the file that gives
 * nrfx this port's critical section and its exclusive-access atomics. The
 * failure is not subtle when it happens at compile time, which it did; it would
 * have been very subtle if the template had happened to compile.
 *
 * Discarding rather than forwarding to woz_freertos_log is deliberate. The one
 * nrfx driver this image compiles is USBD, whose logging is per-packet on a
 * 1 kHz frame clock; routing that to an RTT channel the radio also writes would
 * change the timing of the thing being observed.
 */
#ifndef NRFX_LOG_H__
#define NRFX_LOG_H__

#define NRFX_LOG_ERROR(format, ...)
#define NRFX_LOG_WARNING(format, ...)
#define NRFX_LOG_INFO(format, ...)
#define NRFX_LOG_DEBUG(format, ...)

#define NRFX_LOG_HEXDUMP_ERROR(p_memory, length)
#define NRFX_LOG_HEXDUMP_WARNING(p_memory, length)
#define NRFX_LOG_HEXDUMP_INFO(p_memory, length)
#define NRFX_LOG_HEXDUMP_DEBUG(p_memory, length)

#define NRFX_LOG_ERROR_STRING_GET(error_code)

#endif /* NRFX_LOG_H__ */
