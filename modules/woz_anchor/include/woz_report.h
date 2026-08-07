/**
 * @file woz_report.h — one range report, as a line of ASCII.
 *
 * Stage C2 of internal/two-anchor-plan.md. The satellite already has a console
 * the door node does not, so the cheapest possible two-anchor data stream is
 * one line per accepted round, read by a host script. No transport firmware, no
 * mesh, no sockets: a cable and a grep.
 *
 * The same struct is what a Stage C3 datagram would carry, so the fields are
 * chosen for the binary format the plan specifies rather than for the text one.
 * That is deliberate -- when the transport changes, only the codec changes.
 *
 * ## The line
 *
 *     ARP1 <anchor> <seq> <us_hi> <us_lo> <d_mm> <q> <trust> <flags> <drop> <acc> *<crc>
 *
 * Space-separated decimals, one trailing CRC-16/CCITT-FALSE in four uppercase
 * hex digits, over every byte before the '*'. Terminated by '\n'.
 *
 * WHY A CHECKSUM ON A CABLE. A UART line that gets corrupted usually fails to
 * parse and is discarded, which is harmless. The case worth defending against
 * is the one that does not: a single flipped digit turns 1004 mm into 1904 mm
 * and stays perfectly well-formed. This feeds a security decision, so a
 * plausible wrong number is worse than an obvious broken one.
 *
 * WHY THE UPTIME IS TWO FIELDS. It is a 64-bit microsecond count, and printing
 * one on an embedded target means depending on the C library having 64-bit
 * integer conversion compiled in -- which is a Kconfig away from not being
 * true, silently, at the point where the line becomes garbage. Splitting it
 * into two 32-bit halves removes the dependency. For the same reason this file
 * does its own decimal conversion and does not call snprintf at all.
 *
 * WHY THE ROUND SEQUENCE IS THE TIMEBASE. Both anchors take part in the same
 * numbered DS-TWR round, so a shared exact index already exists and no clock
 * needs synchronising. The uptime rides along only so drift can be measured,
 * and a later step can decide whether it matters.
 */
#ifndef WOZ_REPORT_H
#define WOZ_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Longest line this codec can emit, including '\n' but not a NUL. */
#define WOZ_REPORT_LINE_MAX 128

/** Prefix every line starts with. Carries the version: see woz_report_parse(). */
#define WOZ_REPORT_MAGIC "ARP1"

/** flags bits. */
#define WOZ_REPORT_F_VALID      0x01u /**< the distance is a real measurement */
#define WOZ_REPORT_F_CALIBRATED 0x02u /**< an antenna-delay constant was applied */
#define WOZ_REPORT_F_DEGRADED   0x04u /**< accepted, but something was off */

/** One accepted DS-TWR round, as the satellite saw it. */
struct woz_range_report {
	uint16_t anchor_id; /**< which satellite */
	uint32_t seq;       /**< round sequence: the shared timebase */
	uint64_t us;        /**< satellite uptime at FINAL RX, microseconds */
	int32_t d_mm;       /**< signed: near-contact rounds can go negative */
	uint16_t quality;   /**< worst STS quality in the run */
	uint8_t trust;      /**< consensus run length */
	uint8_t flags;      /**< WOZ_REPORT_F_* */
	uint32_t dropped;   /**< rounds lost since boot */
	uint32_t accepted;  /**< rounds accepted since boot */
};

/**
 * Render one report as a line.
 *
 * @param r   Report to render.
 * @param out Destination; not NUL-terminated, the line ends with '\n'.
 * @param cap Bytes available at @p out.
 * @return Bytes written, or -EINVAL on a NULL argument, or -ENOSPC if @p cap is
 *         smaller than the line needs. Never writes a partial line.
 */
int woz_report_format(const struct woz_range_report *r, char *out, size_t cap);

/**
 * Parse a line back into a report.
 *
 * Rejects rather than reinterprets: an unknown magic is refused outright, the
 * way the range-integrity rules refuse a v2 frame instead of guessing at it
 * (docs/range-integrity.md:58-59). A future ARP2 will not be silently read as
 * an ARP1 with extra fields.
 *
 * @param line Line contents; a trailing '\n' or "\r\n" is optional.
 * @param len  Bytes at @p line.
 * @param out  Filled only on success.
 * @return 0 on success, -EINVAL on a NULL argument, -EBADMSG on a malformed
 *         line, a bad CRC, a field out of range, or an unrecognised magic.
 */
int woz_report_parse(const char *line, size_t len, struct woz_range_report *out);

/** CRC-16/CCITT-FALSE, exposed so a host-side consumer can agree with us. */
uint16_t woz_report_crc16(const char *data, size_t len);

#endif /* WOZ_REPORT_H */
