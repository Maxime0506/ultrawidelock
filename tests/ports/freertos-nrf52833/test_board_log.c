/*
 * The board's log sink, as an RTT up-buffer.
 *
 * The test stands in for the J-Link: it reads the ring the way a host does, by
 * taking bytes between the read and write offsets and advancing the read offset
 * itself. Nothing here calls into the sink's internals, so what is checked is
 * the buffer a real host would see.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultrawidelock_freertos_platform.h>

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                    \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                              \
			printf("  FAIL %s\n", (label));                                            \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                            \
		}                                                                                  \
	} while (0)

/*
 * The control block, declared exactly as the sink defines it. Repeating the
 * layout here rather than sharing a header is deliberate: it is a wire format,
 * and a test that imported the producer's own definition could not notice the
 * producer changing it.
 */
struct rtt_buffer_up {
	const char *name;
	char *buffer;
	unsigned size;
	unsigned write_offset;
	volatile unsigned read_offset;
	unsigned flags;
};

struct rtt_buffer_down {
	const char *name;
	char *buffer;
	unsigned size;
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

extern struct rtt_control_block ultrawidelock_freertos_rtt_control_block;

#define CB ultrawidelock_freertos_rtt_control_block
#define UP (&CB.up[0])

/* Read the ring the way a J-Link does. Returns bytes drained. */
static size_t host_drain(char *out, size_t capacity)
{
	size_t used = 0;

	while (UP->read_offset != UP->write_offset && used + 1u < capacity) {
		out[used++] = UP->buffer[UP->read_offset];
		UP->read_offset = (UP->read_offset + 1u) % UP->size;
	}
	out[used] = '\0';
	return used;
}

/* Empty the ring without reading it. */
static void host_reset(void)
{
	UP->write_offset = 0;
	UP->read_offset = 0;
}

/* ---- the control block -------------------------------------------------- */

static void test_control_block(void)
{
	static const char expect_id[16] = { 'S', 'E', 'G', 'G', 'E', 'R', ' ', 'R',
					    'T', 'T', 0,   0,   0,   0,   0,   0 };

	CHECK("the identifier is the one a J-Link searches for",
	      memcmp(CB.id, expect_id, sizeof(expect_id)) == 0);
	CHECK("one up-buffer is advertised", CB.max_up_buffers == 1);
	CHECK("one down-buffer is advertised", CB.max_down_buffers == 1);
	CHECK("the up-buffer has storage", UP->buffer != NULL && UP->size > 0u);
	CHECK("the up-buffer is named", UP->name != NULL && strcmp(UP->name, "Terminal") == 0);
	/*
	 * Mode zero is no-block-skip. Anything else can stall the caller, and a
	 * log call that blocks at a radio priority overruns a radio event.
	 */
	CHECK("the up-buffer never blocks the writer", UP->flags == 0u);

	/* The field order is the wire format; the target build pins the offsets. */
	CHECK("the up-buffer field order matches the format",
	      offsetof(struct rtt_buffer_up, name) < offsetof(struct rtt_buffer_up, buffer) &&
		      offsetof(struct rtt_buffer_up, buffer) <
			      offsetof(struct rtt_buffer_up, size) &&
		      offsetof(struct rtt_buffer_up, size) <
			      offsetof(struct rtt_buffer_up, write_offset) &&
		      offsetof(struct rtt_buffer_up, write_offset) <
			      offsetof(struct rtt_buffer_up, read_offset) &&
		      offsetof(struct rtt_buffer_up, read_offset) <
			      offsetof(struct rtt_buffer_up, flags));
}

/* ---- ordinary lines ----------------------------------------------------- */

static void test_lines(void)
{
	char out[512];

	host_reset();

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "radio", "started at %u", 42u);
	(void)host_drain(out, sizeof(out));
	CHECK("a line carries its level", strncmp(out, "I ", 2) == 0);
	CHECK("a line carries its tag", strstr(out, "radio: ") != NULL);
	CHECK("a line carries its formatted message", strstr(out, "started at 42") != NULL);
	CHECK("a line ends with a newline", out[strlen(out) - 1u] == '\n');
	CHECK("a line is one line", strchr(out, '\n') == &out[strlen(out) - 1u]);

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, "ble", "down");
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_WARNING, "ble", "retry");
	(void)host_drain(out, sizeof(out));
	CHECK("lines arrive in order", strstr(out, "E ble: down\nW ble: retry\n") != NULL);

	/* A null tag must not be dereferenced. */
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, NULL, "no tag");
	(void)host_drain(out, sizeof(out));
	CHECK("a null tag is survivable", strstr(out, "no tag") != NULL);

	/* A null format is refused rather than passed to vsnprintf. */
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "x", NULL);
	CHECK("a null format writes nothing", host_drain(out, sizeof(out)) == 0u);
}

/* ---- the level gate ----------------------------------------------------- */

static void test_level_gate(void)
{
	char out[512];

	host_reset();

	/* The default ceiling is INFO, so DEBUG costs the ring nothing. */
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_DEBUG, "uwb", "chatter");
	CHECK("a level above the ceiling writes nothing", host_drain(out, sizeof(out)) == 0u);

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "uwb", "kept");
	CHECK("a level at the ceiling is written", host_drain(out, sizeof(out)) > 0u);
}

/* ---- the ring ----------------------------------------------------------- */

static void test_wrap(void)
{
	char out[2048];
	unsigned i;
	size_t drained;

	host_reset();

	/*
	 * Push the write offset near the end, then write across it. A ring that
	 * copies in one piece would run off the end of its storage here.
	 */
	UP->write_offset = UP->size - 8u;
	UP->read_offset = UP->size - 8u;

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "wrap", "abcdefghijklmnop");
	drained = host_drain(out, sizeof(out));
	CHECK("a line that straddles the end is drained whole", drained > 0u);
	CHECK("a straddling line reassembles in order",
	      strstr(out, "I wrap: abcdefghijklmnop\n") != NULL);

	/* Many lines through the wrap, to prove the offsets stay consistent. */
	host_reset();
	for (i = 0; i < 200u; i++) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "n", "%u", i);
		(void)host_drain(out, sizeof(out));
	}
	CHECK("the ring survives many wraps", strstr(out, "I n: 199\n") != NULL);
}

static void test_full_ring(void)
{
	char out[2048];
	unsigned write_before;

	host_reset();

	/*
	 * Leave less room than a line needs. In no-block-skip mode the line must
	 * be dropped whole: a partial line would splice with whatever the next
	 * writer put there, and a log that invents lines is worse than one that
	 * admits it lost some.
	 */
	UP->write_offset = 0;
	UP->read_offset = 4u;

	write_before = UP->write_offset;
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "full",
				   "a line far longer than four bytes");
	CHECK("a line that does not fit is dropped whole", UP->write_offset == write_before);

	/* And the ring is still coherent afterwards. */
	host_reset();
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "after", "ok");
	(void)host_drain(out, sizeof(out));
	CHECK("the ring still works after a dropped line", strstr(out, "after: ok") != NULL);
}

/* The write offset must never reach the read offset: equal reads as empty. */
static void test_never_fills_completely(void)
{
	unsigned i;
	bool collided = false;

	host_reset();
	/* The host never reads, so the ring fills and then refuses. */
	for (i = 0; i < 400u; i++) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "fill",
					   "%u padding padding padding", i);
		if (UP->write_offset == UP->read_offset && i > 0u) {
			collided = true;
		}
	}
	CHECK("a full ring never reads as an empty one", !collided);
	CHECK("an unread ring stops accepting rather than overwriting",
	      UP->write_offset != UP->read_offset);

	/*
	 * The exact case, engineered rather than stumbled into: a line that
	 * would land the write offset precisely on the read offset. The ring has
	 * to refuse it, because equal offsets are how a host is told the buffer
	 * is empty -- accepting it would present a full ring as an empty one and
	 * lose every byte in it.
	 */
	host_reset();
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "size", "measure me");
	{
		unsigned line_length = UP->write_offset;

		host_reset();
		UP->write_offset = 0;
		UP->read_offset = line_length;

		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "size", "measure me");
		CHECK("a line that would exactly fill the ring is refused",
		      UP->write_offset != UP->read_offset);
	}
}

/* ---- long lines --------------------------------------------------------- */

static void test_truncation(void)
{
	char out[2048];
	char huge[512];

	host_reset();
	memset(huge, 'x', sizeof(huge) - 1u);
	huge[sizeof(huge) - 1u] = '\0';

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "long", "%s", huge);
	(void)host_drain(out, sizeof(out));
	CHECK("an over-long line is still written", strlen(out) > 0u);
	CHECK("an over-long line is truncated, not split", out[strlen(out) - 1u] == '\n');
	CHECK("an over-long line is one line", strchr(out, '\n') == &out[strlen(out) - 1u]);
}

/* ---- hexdump ------------------------------------------------------------ */

static void test_hexdump(void)
{
	char out[2048];
	static const uint8_t data[] = { 0x00, 0x0f, 0xa5, 0xff };

	host_reset();
	ultrawidelock_freertos_log_hexdump(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "apdu", data,
					   sizeof(data), "select");
	(void)host_drain(out, sizeof(out));

	CHECK("a hexdump names its message and length", strstr(out, "select (4 bytes)") != NULL);
	CHECK("a hexdump renders low bytes with both digits", strstr(out, "00 0f ") != NULL);
	CHECK("a hexdump renders high bytes", strstr(out, "a5 ff") != NULL);

	host_reset();
	ultrawidelock_freertos_log_hexdump(ULTRAWIDELOCK_FREERTOS_LOG_INFO, "apdu", NULL, 4u, "absent");
	(void)host_drain(out, sizeof(out));
	CHECK("a null hexdump body writes only its header",
	      strstr(out, "absent (4 bytes)") != NULL && strchr(out, '\n') == &out[strlen(out) - 1u]);
}

int main(void)
{
	test_control_block();
	test_lines();
	test_level_gate();
	test_wrap();
	test_full_ring();
	test_never_fills_completely();
	test_truncation();
	test_hexdump();

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
