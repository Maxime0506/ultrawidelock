/**
 * @file test_ultrawidelock_report.c — ultrawidelock_report suite: the C2 line codec.
 *
 * A codec suite is mostly round trips, and round trips prove almost nothing on
 * their own: a codec that drops a field symmetrically passes every one of them.
 * So the assertions that matter here are the ones about lines this codec did
 * NOT produce -- corrupted digits, a foreign version, truncation, and the
 * boundary values that a hand-written decimal converter gets wrong.
 */
#include "test.h"

#include "ultrawidelock_report.h"

#include <errno.h>
#include <string.h>

static struct woz_range_report sample(void)
{
	struct woz_range_report r;

	r.anchor_id = 2u;
	r.seq = 1234u;
	r.us = 0x0000000123456789ULL;
	r.d_mm = 1004;
	r.quality = 87u;
	r.trust = 5u;
	r.flags = ULTRAWIDELOCK_REPORT_F_VALID | ULTRAWIDELOCK_REPORT_F_CALIBRATED;
	r.dropped = 3u;
	r.accepted = 991u;
	return r;
}

static bool same(const struct woz_range_report *a, const struct woz_range_report *b)
{
	return a->anchor_id == b->anchor_id && a->seq == b->seq && a->us == b->us &&
	       a->d_mm == b->d_mm && a->quality == b->quality && a->trust == b->trust &&
	       a->flags == b->flags && a->dropped == b->dropped && a->accepted == b->accepted;
}

/*
 * Pin the CRC against the published check value rather than against our own
 * output. Any decoder on the other end only interoperates for as long as the
 * two agree; anchoring both to the standard's
 * "123456789" -> 0x29B1 is what makes that agreement checkable instead of
 * assumed. If this fails, every C2 line is being dropped as corrupt.
 */
static void crc_matches_the_standard(void)
{
	t_group("report: CRC-16/CCITT-FALSE against the published check value");

	T_EQ("crc.check_value", ultrawidelock_report_crc16("123456789", 9u), 0x29B1u);
}

static void round_trip(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;

	t_group("report: a formatted line parses back to the same report");

	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("rt.formatted", n > 0);
	T_OK("rt.newline", n > 0 && buf[n - 1] == '\n');
	T_OK("rt.magic", n > 4 && memcmp(buf, "ARP1", 4) == 0);
	T_EQ("rt.parsed", ultrawidelock_report_parse(buf, (size_t)n, &back), 0);
	T_OK("rt.identical", same(&r, &back));
}

/*
 * The values a hand-rolled decimal converter gets wrong. INT32_MIN especially:
 * negating it in 32-bit arithmetic overflows, and the natural implementation
 * emits a positive number with no warning.
 */
static void extreme_values(void)
{
	struct woz_range_report r;
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;

	t_group("report: boundary values survive the codec");

	/* Everything at its maximum, including a full 64-bit uptime. */
	r.anchor_id = 65535u;
	r.seq = 4294967295u;
	r.us = 0xFFFFFFFFFFFFFFFFULL;
	r.d_mm = 2147483647;
	r.quality = 65535u;
	r.trust = 255u;
	r.flags = 255u;
	r.dropped = 4294967295u;
	r.accepted = 4294967295u;
	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("ext.max.fits", n > 0 && (size_t)n <= ULTRAWIDELOCK_REPORT_LINE_MAX);
	T_EQ("ext.max.parses", ultrawidelock_report_parse(buf, (size_t)n, &back), 0);
	T_OK("ext.max.same", same(&r, &back));

	/* INT32_MIN, the one that overflows if negated in 32 bits. */
	r = sample();
	r.d_mm = -2147483647 - 1;
	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("ext.min.formatted", n > 0);
	T_EQ("ext.min.parses", ultrawidelock_report_parse(buf, (size_t)n, &back), 0);
	T_EQ("ext.min.value", back.d_mm, -2147483647 - 1);

	/* Near-contact rounds go slightly negative; that is a real measurement. */
	r.d_mm = -47;
	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_EQ("ext.neg.parses", ultrawidelock_report_parse(buf, (size_t)n, &back), 0);
	T_EQ("ext.neg.value", back.d_mm, -47);

	/* All zeroes, where a "skip leading zeros" loop emits nothing at all. */
	memset(&r, 0, sizeof(r));
	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("ext.zero.formatted", n > 0);
	T_EQ("ext.zero.parses", ultrawidelock_report_parse(buf, (size_t)n, &back), 0);
	T_OK("ext.zero.same", same(&r, &back));
}

/*
 * THE CASE THE CHECKSUM EXISTS FOR. A flipped digit that leaves the line
 * perfectly well-formed. Without the CRC this parses cleanly and hands a
 * security decision a wrong distance.
 */
static void single_flipped_digit_is_caught(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;
	int i;
	int mutated = 0;
	int caught = 0;

	t_group("report: a corrupted digit is refused, not believed");

	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("flip.formatted", n > 0);

	/* Walk the whole payload and bump every digit by one. */
	for (i = 0; i < n; i++) {
		char save = buf[i];

		if (save < '0' || save > '8') {
			continue;
		}
		buf[i] = (char)(save + 1);
		mutated++;
		if (ultrawidelock_report_parse(buf, (size_t)n, &back) != 0) {
			caught++;
		}
		buf[i] = save;
	}
	T_OK("flip.mutations_exist", mutated >= 10);
	T_EQ("flip.all_caught", caught, mutated);
}

static void foreign_version_is_refused(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;

	t_group("report: an unknown magic is refused, not reinterpreted");

	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	buf[3] = '2'; /* ARP1 -> ARP2 */
	T_EQ("ver.arp2_refused", ultrawidelock_report_parse(buf, (size_t)n, &back), -EBADMSG);

	buf[0] = 'X';
	buf[3] = '1';
	T_EQ("ver.junk_refused", ultrawidelock_report_parse(buf, (size_t)n, &back), -EBADMSG);

	/* Console noise must not parse as a report. */
	{
		const char *noise = "[00:00:01.234] <inf> main: ANCHOR rounds=100\n";

		T_EQ("ver.log_line", ultrawidelock_report_parse(noise, strlen(noise), &back), -EBADMSG);
	}
}

static void truncation_and_junk(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	char cut[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;
	int i;
	int accepted = 0;

	t_group("report: every truncation of a good line is refused");

	n = ultrawidelock_report_format(&r, buf, sizeof(buf));
	/* A UART that drops the tail of a line is ordinary. Not one prefix of a
	 * valid line may parse as a shorter valid one.
	 *
	 * Stops at n-1 because dropping only the trailing '\n' is not truncation
	 * -- an unterminated line is explicitly valid, and line_endings() asserts
	 * that on purpose. */
	for (i = 1; i < n - 1; i++) {
		memcpy(cut, buf, (size_t)i);
		if (ultrawidelock_report_parse(cut, (size_t)i, &back) == 0) {
			accepted++;
		}
	}
	T_EQ("trunc.none_accepted", accepted, 0);

	/* A field missing entirely, CRC recomputed over the shortened payload,
	 * is still refused: the parser counts fields, not just checksum. */
	{
		const char *short_line = "ARP1 2 1234 0 5 *0000";

		T_EQ("trunc.too_few_fields",
		     ultrawidelock_report_parse(short_line, strlen(short_line), &back), -EBADMSG);
	}

	/* Extra trailing junk inside the covered range. */
	{
		const char *tail = "ARP1 2 1234 0 305419896 1004 87 5 3 3 991 xyz *0000";

		T_EQ("trunc.trailing_junk", ultrawidelock_report_parse(tail, strlen(tail), &back), -EBADMSG);
	}
}

static void line_endings(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	char crlf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	int n;

	t_group("report: LF, CRLF and a bare line all parse");

	n = ultrawidelock_report_format(&r, buf, sizeof(buf));

	/* CRLF, which is what a terminal program may hand back. */
	memcpy(crlf, buf, (size_t)n - 1);
	crlf[n - 1] = '\r';
	crlf[n] = '\n';
	T_EQ("eol.crlf", ultrawidelock_report_parse(crlf, (size_t)n + 1, &back), 0);
	T_OK("eol.crlf_same", same(&r, &back));

	/* No terminator at all. */
	T_EQ("eol.none", ultrawidelock_report_parse(buf, (size_t)n - 1, &back), 0);
	T_OK("eol.none_same", same(&r, &back));
}

static void small_buffer_never_writes_a_partial_line(void)
{
	struct woz_range_report r = sample();
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];
	size_t cap;
	int n;
	int full;

	t_group("report: too small a buffer fails, and fails cleanly");

	full = ultrawidelock_report_format(&r, buf, sizeof(buf));
	T_OK("cap.full_ok", full > 0);

	/* Every capacity below what the line needs must fail. A codec that
	 * emitted a truncated line here would put a half-report on the wire, and
	 * a truncated line whose CRC happens to match is the one thing the CRC
	 * cannot save us from. */
	for (cap = 0u; cap < (size_t)full; cap++) {
		n = ultrawidelock_report_format(&r, buf, cap);
		if (n >= 0) {
			T_OK("cap.no_partial", false);
			break;
		}
	}
	if (cap == (size_t)full) {
		T_OK("cap.no_partial", true);
	}
	T_EQ("cap.exact_fits", ultrawidelock_report_format(&r, buf, (size_t)full), full);
}

static void rejects_bad_input(void)
{
	struct woz_range_report r = sample();
	struct woz_range_report back;
	char buf[ULTRAWIDELOCK_REPORT_LINE_MAX];

	t_group("report: NULLs and empties");

	T_EQ("in.null_report", ultrawidelock_report_format(NULL, buf, sizeof(buf)), -EINVAL);
	T_EQ("in.null_buf", ultrawidelock_report_format(&r, NULL, sizeof(buf)), -EINVAL);
	T_EQ("in.null_line", ultrawidelock_report_parse(NULL, 10u, &back), -EINVAL);
	T_EQ("in.null_out", ultrawidelock_report_parse("ARP1", 4u, NULL), -EINVAL);
	T_EQ("in.empty", ultrawidelock_report_parse("", 0u, &back), -EBADMSG);

	/* The CRC helper is used by the host-side consumer, so its degenerate
	 * cases are part of the contract. */
	T_EQ("in.crc_null", ultrawidelock_report_crc16(NULL, 4u), 0u);
	T_EQ("in.crc_empty", ultrawidelock_report_crc16("", 0u), 0xFFFFu);
}

void test_ultrawidelock_report(void)
{
	crc_matches_the_standard();
	round_trip();
	extreme_values();
	single_flipped_digit_is_caught();
	foreign_version_is_refused();
	truncation_and_junk();
	line_endings();
	small_buffer_never_writes_a_partial_line();
	rejects_bad_input();
}
