/*
 * The board's log sink, as a SEGGER RTT up-buffer.
 *
 * RTT and not the UART, and that is the board's decision rather than a
 * preference: uart0 is the J-Link OB's virtual COM port and MCUboot's serial
 * recovery rides it, so an application console there would collide with the DFU
 * path. The Zephyr oracle makes the same choice for the same reason and leaves
 * uart0 free.
 *
 * The control block is written here against the published RTT layout rather
 * than by linking SEGGER's implementation, which is licensed for use with
 * SEGGER's own products. Nothing is vendored and no third-party source is
 * compiled; a J-Link finds this by scanning RAM for the identifier, exactly as
 * it finds SEGGER's.
 *
 * The buffer is in no-block-skip mode, which is the only safe choice here. A
 * blocking sink would stall whatever context called it, and at 115200 baud an
 * 80-character line is about 7 ms against a 1.836 ms DW3110 response-arm
 * deadline. In this mode a write that does not fit is dropped whole, so a line
 * is either complete or absent, never spliced. With no debugger attached the
 * host never advances the read offset, the buffer fills once, and every later
 * write costs a bounds check -- which is the right standing cost for a product
 * that ships without a console.
 *
 * A write masks interrupts for the length of the copy, because the write offset
 * may only be published once the bytes behind it are valid and a single offset
 * cannot carry two writers at once. That is roughly a microsecond per hundred
 * bytes at 64 MHz, so this hook must not be called from the radio's own
 * priority-zero handlers.
 */
/*
 * BEFORE EVERY INCLUDE, and that position is the whole point.
 *
 * This file DEFINES the sinks, so it must see their plain declarations; the
 * header otherwise puts same-named macros over them to gate on level at the
 * call site, and those macros rewrite a function definition into something that
 * does not compile.
 *
 * It cannot sit next to the ultrawidelock_freertos_platform.h include below, because that
 * is not the first time this file reaches that header: nrfx.h pulls in the
 * board's nrfx_glue.h, which includes it. An opt-out placed after nrfx.h is an
 * opt-out that arrives too late, and the error it produces points at the header
 * rather than at the include order that caused it.
 */
#define ULTRAWIDELOCK_FREERTOS_LOG_NO_MACRO 1

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nrfx.h>

#include <ultrawidelock_freertos_platform.h>

/*
 * Bytes of RAM the up-buffer holds. One boot's worth of bring-up chatter.
 *
 * IT IS NOT A RING THAT OVERWRITES. The flags below are RTT_MODE_NO_BLOCK_SKIP,
 * so once the buffer is full a write that does not fit is DROPPED WHOLE and the
 * ones already there are kept. With a debugger attached that never happens,
 * because the host advances the read offset as it reads. With nothing attached
 * -- which is every board not on a bench, and also a board being read by
 * savebin rather than by an RTT client -- the buffer fills once and every line
 * after it is silently lost.
 *
 * That is the right trade for a product: dropping log lines is better than
 * blocking a lock's boot, and better than losing the START of a boot, which is
 * where the reasons live. It is the wrong trade while debugging something
 * talkative, and it cost a bench session to recognise: the Matter advertising
 * decision logs about 1.1 kB into boot, so the line was emitted, dropped, and
 * its absence read as the code not running. It was running -- a BLE scan saw
 * the advertisement on air.
 *
 * Raise this from the build for anything past bring-up. The whole size is
 * static RAM on a part with roughly 8 kB spare, so it is a bench value, not a
 * shipping one, and there is deliberately no default here that spends it.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_LOG_RTT_BUFFER_BYTES
#define ULTRAWIDELOCK_FREERTOS_LOG_RTT_BUFFER_BYTES 1024u
#endif

/* The longest single line. Anything past this is truncated, not split. */
#ifndef ULTRAWIDELOCK_FREERTOS_LOG_LINE_BYTES
#define ULTRAWIDELOCK_FREERTOS_LOG_LINE_BYTES 160u
#endif

/*
 * ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL is defined by ultrawidelock_freertos_platform.h, which is
 * where the call-site gate lives. The runtime comparisons kept below are a
 * backstop for the two calls this file makes with a computed level, and for
 * anything that reaches the sink through a function pointer.
 */

/*
 * The RTT control block, laid out as the published format specifies. A J-Link
 * locates it by searching RAM for acID, so the field order and widths here are
 * a wire format and not an implementation detail: changing one silently stops
 * the host finding the buffer.
 */
struct rtt_buffer_up {
	const char *name;
	char *buffer;
	unsigned size;
	unsigned write_offset;
	/* The host writes this one. */
	volatile unsigned read_offset;
	unsigned flags;
};

struct rtt_buffer_down {
	const char *name;
	char *buffer;
	unsigned size;
	/* The host writes this one. */
	volatile unsigned write_offset;
	unsigned read_offset;
	unsigned flags;
};

struct rtt_control_block {
	char id[16];
	int max_up_buffers;
	int max_down_buffers;
	struct rtt_buffer_up up[1];
	struct rtt_buffer_down down[1];
};

/* Drop a write that does not fit rather than blocking or trimming it. */
#define RTT_MODE_NO_BLOCK_SKIP 0u

/*
 * The layout is what a J-Link expects to find, so it is pinned here. These fire
 * only on a 32-bit target, which is where it matters; the host test builds with
 * 64-bit pointers and checks field order instead.
 */
#if UINTPTR_MAX == 0xffffffffu
_Static_assert(offsetof(struct rtt_buffer_up, name) == 0u, "RTT up-buffer layout");
_Static_assert(offsetof(struct rtt_buffer_up, buffer) == 4u, "RTT up-buffer layout");
_Static_assert(offsetof(struct rtt_buffer_up, size) == 8u, "RTT up-buffer layout");
_Static_assert(offsetof(struct rtt_buffer_up, write_offset) == 12u, "RTT up-buffer layout");
_Static_assert(offsetof(struct rtt_buffer_up, read_offset) == 16u, "RTT up-buffer layout");
_Static_assert(offsetof(struct rtt_buffer_up, flags) == 20u, "RTT up-buffer layout");
_Static_assert(sizeof(struct rtt_buffer_up) == 24u, "RTT up-buffer layout");
_Static_assert(sizeof(struct rtt_buffer_down) == 24u, "RTT down-buffer layout");
_Static_assert(offsetof(struct rtt_control_block, max_up_buffers) == 16u, "RTT header layout");
_Static_assert(offsetof(struct rtt_control_block, max_down_buffers) == 20u, "RTT header layout");
_Static_assert(offsetof(struct rtt_control_block, up) == 24u, "RTT header layout");
#endif

static char s_up_storage[ULTRAWIDELOCK_FREERTOS_LOG_RTT_BUFFER_BYTES];

/*
 * Not static: a J-Link scans RAM for the identifier, and keeping the symbol
 * visible also lets a debugger read the ring by name when no host is attached.
 */
struct rtt_control_block ultrawidelock_freertos_rtt_control_block = {
	.id = { 'S', 'E', 'G', 'G', 'E', 'R', ' ', 'R', 'T', 'T', 0, 0, 0, 0, 0, 0 },
	.max_up_buffers = 1,
	.max_down_buffers = 1,
	.up = { {
		.name = "Terminal",
		.buffer = s_up_storage,
		.size = ULTRAWIDELOCK_FREERTOS_LOG_RTT_BUFFER_BYTES,
		.write_offset = 0,
		.read_offset = 0,
		.flags = RTT_MODE_NO_BLOCK_SKIP,
	} },
	.down = { {
		.name = "Terminal",
		.buffer = NULL,
		.size = 0,
		.write_offset = 0,
		.read_offset = 0,
		.flags = RTT_MODE_NO_BLOCK_SKIP,
	} },
};

/*
 * Bytes that may be written without the write offset reaching the read offset.
 * One byte is always left free, because a ring whose two offsets are equal
 * reads as empty and a completely full ring would be indistinguishable from it.
 */
static unsigned space_available(const struct rtt_buffer_up *up)
{
	unsigned read_offset = up->read_offset;

	if (read_offset > up->write_offset) {
		return read_offset - up->write_offset - 1u;
	}
	return up->size - up->write_offset + read_offset - 1u;
}

/*
 * Copy into the ring and publish. The caller holds interrupts masked, so the
 * offset is advanced only after every byte behind it is in place.
 */
static void ring_write(struct rtt_buffer_up *up, const char *data, unsigned length)
{
	unsigned first = up->size - up->write_offset;

	if (first > length) {
		first = length;
	}
	memcpy(&up->buffer[up->write_offset], data, first);
	if (length > first) {
		memcpy(&up->buffer[0], &data[first], length - first);
	}
	up->write_offset = (up->write_offset + length) % up->size;
}

/*
 * One whole line or nothing. A partial line in the ring would be spliced with
 * whatever the next writer put there, and a log that invents lines is worse
 * than one that admits it lost some.
 */
static void emit(const char *data, size_t length)
{
	struct rtt_buffer_up *up = &ultrawidelock_freertos_rtt_control_block.up[0];
	uint32_t primask = __get_PRIMASK();

	if (length == 0u || length >= up->size) {
		return;
	}

	__disable_irq();
	if (space_available(up) >= (unsigned)length) {
		ring_write(up, data, (unsigned)length);
	}
	__set_PRIMASK(primask);
}

static const char *level_prefix(enum ultrawidelock_freertos_log_level level)
{
	switch (level) {
	case ULTRAWIDELOCK_FREERTOS_LOG_ERROR:
		return "E";
	case ULTRAWIDELOCK_FREERTOS_LOG_WARNING:
		return "W";
	case ULTRAWIDELOCK_FREERTOS_LOG_INFO:
		return "I";
	case ULTRAWIDELOCK_FREERTOS_LOG_DEBUG:
		return "D";
	default:
		return NULL;
	}
}

void ultrawidelock_freertos_log_va(enum ultrawidelock_freertos_log_level level, const char *tag, const char *fmt,
			 va_list args)
{
	char line[ULTRAWIDELOCK_FREERTOS_LOG_LINE_BYTES];
	const char *prefix;
	int header;
	int body;
	size_t used;

	if (level > ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL || fmt == NULL) {
		return;
	}

	prefix = level_prefix(level);
	if (prefix == NULL) {
		header = 0;
	} else {
		header = snprintf(line, sizeof(line), "%s %s: ", prefix, tag != NULL ? tag : "-");
		if (header < 0) {
			return;
		}
		if ((size_t)header >= sizeof(line)) {
			header = (int)sizeof(line) - 1;
		}
	}

	body = vsnprintf(&line[header], sizeof(line) - (size_t)header, fmt, args);
	if (body < 0) {
		return;
	}

	/*
	 * vsnprintf reports what it would have written. Clamp to what it did, so
	 * an over-long line is truncated here rather than read past.
	 */
	used = (size_t)header + (size_t)body;
	if (used > sizeof(line) - 2u) {
		used = sizeof(line) - 2u;
	}
	line[used] = '\n';
	used++;

	emit(line, used);
}

/* The variadic spelling every call site but OpenThread's uses. Kept as the thin
 * one so there is a single formatter: otPlatLog is handed a va_list it cannot
 * unpack, which is the whole reason the _va form is exported at all. */
void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ultrawidelock_freertos_log_va(level, tag, fmt, args);
	va_end(args);
}

void ultrawidelock_freertos_log_hexdump(enum ultrawidelock_freertos_log_level level, const char *tag, const void *data,
			      size_t len, const char *message)
{
	static const char digits[] = "0123456789abcdef";
	const uint8_t *bytes = data;
	char line[ULTRAWIDELOCK_FREERTOS_LOG_LINE_BYTES];
	size_t offset;

	if (level > ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL) {
		return;
	}
	ultrawidelock_freertos_log(level, tag, "%s (%u bytes)", message != NULL ? message : "hexdump",
			 (unsigned)len);
	if (bytes == NULL) {
		return;
	}

	/* Sixteen bytes a line, which is three characters each plus a newline. */
	for (offset = 0; offset < len; offset += 16u) {
		size_t chunk = len - offset;
		size_t used = 0;
		size_t i;

		if (chunk > 16u) {
			chunk = 16u;
		}
		for (i = 0; i < chunk && used + 3u < sizeof(line); i++) {
			line[used++] = digits[bytes[offset + i] >> 4];
			line[used++] = digits[bytes[offset + i] & 0x0fu];
			line[used++] = ' ';
		}
		line[used++] = '\n';
		emit(line, used);
	}
}
