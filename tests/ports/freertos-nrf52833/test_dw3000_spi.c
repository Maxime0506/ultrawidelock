/*
 * The DW3110 SPI backend, against a register-level SPIM and GPIO model.
 *
 * The model is not permissive. It refuses to clock anything when the peripheral
 * is disabled, when END was left set from the previous transfer, when the clock
 * pin's input buffer is disconnected, when an EasyDMA pointer is in flash, or
 * when the send and receive buffers overlap. Every one of those is a bug that a
 * cooperative fake would let through and that hardware would then show as a
 * chip which simply does not answer.
 *
 * The transfers are checked from the bus side rather than from the driver's
 * return value: what went out on MOSI, in what order, with chip select held
 * low across all of it. That is what the DW3110 sees, and it is the only thing
 * the port is actually responsible for getting right.
 *
 * Each scenario runs in its own process. The backend keeps the bus lock and the
 * ready flag in statics, so resetting the peripheral model underneath them
 * would leave the port believing it had configured a peripheral that is now
 * blank -- and every transfer after that would be refused for a reason the
 * scenario never asked about.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fake_freertos.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>

#include <woz_freertos_platform.h>

#include "board_pins.h"
#include "dw3000_spi.h"

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

/* ---- platform stubs ----------------------------------------------------- */

static uint64_t g_busy_wait_us;
static unsigned g_busy_waits;
static unsigned g_errors_logged;

void woz_freertos_log(enum woz_freertos_log_level level, const char *tag, const char *fmt, ...)
{
	(void)tag;
	(void)fmt;
	if (level == WOZ_FREERTOS_LOG_ERROR) {
		g_errors_logged++;
	}
}

void woz_freertos_busy_wait_us(uint64_t us)
{
	g_busy_wait_us = us;
	g_busy_waits++;
}

/* ---- helpers ------------------------------------------------------------ */

/*
 * The bus records every byte of every transfer, so a check reads the slice it
 * cares about rather than resetting the model between commands.
 */
static const uint8_t *mosi_at(size_t offset)
{
	return fake_spim.mosi + offset;
}

static void bus_reset(void)
{
	fake_spim.mosi_len = 0;
	fake_spim.transfers = 0;
	memset(fake_spim.mosi, 0, sizeof(fake_spim.mosi));
}

static void bring_up(void)
{
	fake_gpio_reset();
	fake_spim_reset();
	fake_spim_cs_pin = ULTRAWIDELOCK_DW3000_PIN_CS;
	CHECK("the bus comes up", dw3000_spi_init() == 0);
}

/* ---- checks ------------------------------------------------------------- */

static void check_init(void)
{
	bring_up();

	CHECK("chip select is an output", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].configured &&
					  fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].dir ==
						  NRF_GPIO_PIN_DIR_OUTPUT);
	CHECK("chip select idles released", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].level);
	/*
	 * The order matters and the level alone cannot show it: a pin made an
	 * output while its OUT register still reads zero drives the line low,
	 * which to this chip is a chip-select strobe before anything is
	 * configured. The port writes the idle level first, so the pin was
	 * already high the one time its level changed.
	 */
	CHECK("chip select never strobed on the way up",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].writes == 1u);

	CHECK("the clock pin is an output",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_SCLK].dir == NRF_GPIO_PIN_DIR_OUTPUT);
	CHECK("the clock pin keeps its input buffer connected",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_SCLK].input == NRF_GPIO_PIN_INPUT_CONNECT);
	CHECK("the clock idles low, as SPI mode 0 requires",
	      !fake_gpio[ULTRAWIDELOCK_DW3000_PIN_SCLK].level);
	CHECK("MOSI is an output",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_MOSI].dir == NRF_GPIO_PIN_DIR_OUTPUT);
	CHECK("MISO is an input", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_MISO].configured &&
				  fake_gpio[ULTRAWIDELOCK_DW3000_PIN_MISO].dir == NRF_GPIO_PIN_DIR_INPUT);
	CHECK("MISO is pulled down",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_MISO].pull == NRF_GPIO_PIN_PULLDOWN);

	CHECK("the peripheral is enabled", fake_spim.enabled);
	CHECK("the pins are routed to the peripheral",
	      fake_spim.psel_sck == ULTRAWIDELOCK_DW3000_PIN_SCLK &&
		      fake_spim.psel_mosi == ULTRAWIDELOCK_DW3000_PIN_MOSI &&
		      fake_spim.psel_miso == ULTRAWIDELOCK_DW3000_PIN_MISO);
	CHECK("the DW3110's SPI mode 0 is selected", fake_spim.mode == NRF_SPIM_MODE_0);
	CHECK("bits go out most significant first",
	      fake_spim.bit_order == NRF_SPIM_BIT_ORDER_MSB_FIRST);
	CHECK("the bus starts at the slow clock the chip needs before its PLL locks",
	      fake_spim.frequency == NRF_SPIM_FREQ_2M);
	CHECK("MOSI idles low while the chip answers a read", fake_spim.orc == 0u);

	/*
	 * Three writes before any configuration, and exactly three: chip select,
	 * the clock and MOSI each get their idle level while the pin is still
	 * undriven. That ordering is the whole reason bringing the bus up cannot
	 * glitch the chip -- a pin that becomes an output while OUT still reads
	 * zero drives the line low first and asks questions later, and on chip
	 * select that low is a strobe.
	 */
	CHECK("every output pin gets its idle level before it starts driving",
	      fake_gpio_unconfigured_writes == 3u);
	CHECK("bringing the bus up broke no peripheral rule", fake_spim_violations() == 0u);
}

/*
 * Bringing the bus up again must do nothing at all. The DW3000 hardware layer
 * calls into here on every reset and wake, and by then the chip has been taken
 * to the fast clock; reconfiguring the peripheral would put it back to 2 MHz
 * underneath a driver that believes it is talking at 8, and every register read
 * after that returns a plausible wrong value rather than an error.
 */
static void check_init_is_idempotent(void)
{
	unsigned writes;

	bring_up();
	dw3000_spi_speed_fast();
	writes = fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].writes;

	CHECK("a second bring-up succeeds", dw3000_spi_init() == 0);
	CHECK("a second bring-up leaves the clock where the driver put it",
	      fake_spim.frequency == NRF_SPIM_FREQ_8M);
	CHECK("a second bring-up leaves chip select alone",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].writes == writes);
}

static void check_write(void)
{
	const uint8_t hdr[2] = {0x81, 0x42};
	const uint8_t body[3] = {0xaa, 0xbb, 0xcc};

	bring_up();
	bus_reset();

	CHECK("a write succeeds", dw3000_spi_write(2, hdr, 3, body) == 0);
	CHECK("the write was one transaction", fake_spim.transfers == 1u);
	CHECK("header and body went out as one command, header first",
	      fake_spim.mosi_len == 5u && memcmp(mosi_at(0), hdr, 2) == 0 &&
		      memcmp(mosi_at(2), body, 3) == 0);
	CHECK("chip select was held low across the whole command", fake_spim_cs_held);
	CHECK("chip select was released afterwards", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].level);

	/*
	 * A second command, because END is what a polled driver waits on and the
	 * peripheral leaves it set. A backend that started without clearing it
	 * would sail through its first transfer and then, from the second one
	 * onwards, return before a single bit had moved.
	 */
	CHECK("a second write succeeds", dw3000_spi_write(2, hdr, 3, body) == 0);
	CHECK("the second write clocked its own bytes", fake_spim.transfers == 2u);
	CHECK("the write broke no peripheral rule", fake_spim_violations() == 0u);
}

static void check_write_crc(void)
{
	const uint8_t hdr[2] = {0x81, 0x42};
	const uint8_t body[2] = {0x01, 0x02};

	bring_up();
	bus_reset();

	CHECK("a CRC write succeeds", dw3000_spi_write_crc(2, hdr, 2, body, 0x5a) == 0);
	CHECK("the CRC byte trails the body inside the same command",
	      fake_spim.mosi_len == 5u && fake_spim.mosi[4] == 0x5a);
	CHECK("the CRC write was still one transaction", fake_spim.transfers == 1u);
	CHECK("the CRC write broke no peripheral rule", fake_spim_violations() == 0u);
}

static void check_read(void)
{
	uint8_t hdr[2] = {0x01, 0x00};
	/* Two bytes clocked out under the header, then the register's value. */
	const uint8_t slave[6] = {0xff, 0xff, 0xde, 0xca, 0x03, 0x00};
	uint8_t out[4];

	bring_up();
	bus_reset();
	fake_spim_respond(slave, sizeof(slave));
	memset(out, 0, sizeof(out));

	CHECK("a read succeeds", dw3000_spi_read(2, hdr, 4, out) == 0);
	CHECK("the read clocked the header then idle bytes",
	      fake_spim.mosi_len == 6u && fake_spim.mosi[0] == 0x01 && fake_spim.mosi[1] == 0x00 &&
		      fake_spim.mosi[2] == 0u && fake_spim.mosi[5] == 0u);
	/*
	 * EasyDMA has one receive pointer and starts filling it with the first
	 * byte on the wire, so the answer sits past the header, not at the front
	 * of the buffer. Reading from offset zero would hand the caller the
	 * bytes the chip clocked out while it was still hearing the address.
	 */
	CHECK("the response is taken from past the header, not from the front",
	      out[0] == 0xde && out[1] == 0xca && out[2] == 0x03 && out[3] == 0x00);
	CHECK("the read broke no peripheral rule", fake_spim_violations() == 0u);
}

static void check_read_does_not_disturb_the_header(void)
{
	uint8_t hdr[2] = {0x05, 0x06};
	const uint8_t slave[4] = {0, 0, 0x77, 0x88};
	uint8_t out[2];

	bring_up();
	bus_reset();
	fake_spim_respond(slave, sizeof(slave));

	CHECK("a read with a caller-owned header succeeds", dw3000_spi_read(2, hdr, 2, out) == 0);
	/*
	 * The decadriver reuses its header buffer across calls and the signature
	 * hands it over non-const, so a backend that received into it would
	 * corrupt the next command's address.
	 */
	CHECK("the caller's header buffer is unchanged", hdr[0] == 0x05 && hdr[1] == 0x06);
}

/*
 * The reason the bounce buffer exists. The decadriver passes const bodies that
 * the linker puts in flash, and EasyDMA cannot read flash -- the transfer does
 * not merely fail, the bus faults. The model refuses any pointer the test has
 * declared to be flash, so a backend that handed the caller's buffer straight
 * to TXD.PTR is caught here and nowhere else.
 */
static void check_flash_body_is_copied(void)
{
	static const uint8_t hdr[2] = {0x81, 0x00};
	static const uint8_t body[4] = {0x10, 0x20, 0x30, 0x40};

	bring_up();
	bus_reset();
	fake_spim_mark_flash(hdr, sizeof(hdr));
	fake_spim_mark_flash(body, sizeof(body));

	CHECK("a write from flash-resident buffers succeeds",
	      dw3000_spi_write(2, hdr, 4, body) == 0);
	CHECK("no EasyDMA pointer reached flash", fake_spim.violations_ram == 0u);
	CHECK("the transfer still ran", fake_spim.transfers == 1u);
	CHECK("the flash-resident bytes still reached the wire",
	      fake_spim.mosi_len == 6u && memcmp(mosi_at(2), body, 4) == 0);
}

static void check_oversize_is_refused(void)
{
	static uint8_t hdr[2];
	static uint8_t body[ULTRAWIDELOCK_DW3000_SPI_XFER_MAX];

	bring_up();
	bus_reset();

	/*
	 * Exactly one byte past the bound, rather than wildly past it. A test
	 * that overshoots by kilobytes is answered by whatever the overrun
	 * happens to corrupt, which is not a check; the boundary is where a
	 * wrong comparison actually lives.
	 */
	CHECK("the largest allowed transfer is accepted",
	      dw3000_spi_write(2, hdr, ULTRAWIDELOCK_DW3000_SPI_XFER_MAX - 2u, body) == 0);
	CHECK("the largest allowed transfer was clocked", fake_spim.transfers == 1u);

	bus_reset();
	/*
	 * Refusing matters more than it looks. A short SPI transfer to this chip
	 * does not report anything -- it returns a different register -- so
	 * truncating instead of refusing would produce plausible wrong values.
	 */
	CHECK("one byte past the bound is refused",
	      dw3000_spi_write(2, hdr, ULTRAWIDELOCK_DW3000_SPI_XFER_MAX - 1u, body) == -1);
	CHECK("a refused transfer clocks nothing", fake_spim.transfers == 0u);
	CHECK("a zero-length header is refused", dw3000_spi_write(0, hdr, 1, body) == -1);
}

static void check_stalled_transfer(void)
{
	const uint8_t hdr[2] = {0x81, 0x00};
	const uint8_t body[2] = {0x01, 0x02};

	bring_up();
	bus_reset();
	g_errors_logged = 0;

	fake_spim.stall = true;
	CHECK("a transfer that never ends fails", dw3000_spi_write(2, hdr, 2, body) == -1);
	CHECK("a failed transfer is reported", g_errors_logged == 1u);
	/*
	 * Walking away is not enough. EasyDMA would still be writing into the
	 * receive buffer that the next transfer is about to reuse, so the port
	 * has to stop the peripheral before it lets go.
	 */
	CHECK("the peripheral was stopped rather than abandoned", fake_spim.stops == 1u);
	CHECK("chip select was released even so", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].level);

	/* And the bus is usable again, which is the point of stopping it. */
	fake_spim.stall = false;
	bus_reset();
	CHECK("the next transfer succeeds", dw3000_spi_write(2, hdr, 2, body) == 0);
	CHECK("the recovered transfer clocked its bytes", fake_spim.mosi_len == 4u);
	CHECK("recovery broke no peripheral rule", fake_spim_violations() == 0u);
}

static void check_speed_switch(void)
{
	const uint8_t hdr[2] = {0x00, 0x00};

	bring_up();

	dw3000_spi_speed_fast();
	CHECK("the fast rate is the 8 MHz the board is qualified for",
	      fake_spim.frequency == NRF_SPIM_FREQ_8M);
	bus_reset();
	CHECK("a transfer at the fast rate succeeds", dw3000_spi_write(2, hdr, 0, NULL) == 0);
	CHECK("the fast transfer ran", fake_spim.transfers == 1u);

	dw3000_spi_speed_slow();
	CHECK("the slow rate is back under the chip's 7 MHz pre-lock limit",
	      fake_spim.frequency == NRF_SPIM_FREQ_2M);
	CHECK("switching rates broke no peripheral rule", fake_spim_violations() == 0u);
}

static void check_wakeup(void)
{
	bring_up();
	g_busy_waits = 0;
	g_busy_wait_us = 0;

	dw3000_spi_wakeup();

	/*
	 * The wake is a chip-select pulse with no clock behind it, which is the
	 * whole reason chip select is a GPIO instead of the peripheral's own
	 * CSN: hardware chip select only asserts around a transfer.
	 */
	CHECK("waking the chip clocked nothing", fake_spim.transfers == 0u);
	CHECK("waking the chip pulsed chip select", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].writes == 3u);
	CHECK("chip select is released after the pulse", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].level);
	CHECK("the pulse is held for the 500 us the chip requires",
	      g_busy_waits == 1u && g_busy_wait_us == 500u);
}

static void check_fini(void)
{
	const uint8_t hdr[2] = {0x00, 0x00};

	bring_up();
	dw3000_spi_fini();

	CHECK("shutting down disables the peripheral", !fake_spim.enabled);
	CHECK("shutting down leaves chip select released",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].level);
	bus_reset();
	CHECK("a transfer after shutdown is refused", dw3000_spi_write(2, hdr, 0, NULL) == -1);
	CHECK("a transfer after shutdown clocks nothing", fake_spim.transfers == 0u);
	/*
	 * Refused by the port, not by the peripheral. Both end in a failure
	 * return, but a backend that starts a disabled SPIM and waits for it
	 * spends its whole timeout bound doing so, on the task that arms the
	 * radio -- and the deadline that matters here is under two milliseconds.
	 */
	CHECK("a transfer after shutdown never touches the disabled peripheral",
	      fake_spim.violations_disabled == 0u);
	CHECK("shutting down twice is harmless", (dw3000_spi_fini(), !fake_spim.enabled));

	/* And the bus comes back, because the chip is reset and probed again. */
	CHECK("the bus can be brought up again", dw3000_spi_init() == 0);
	CHECK("the revived bus is enabled", fake_spim.enabled);
	bus_reset();
	CHECK("the revived bus transfers", dw3000_spi_write(2, hdr, 0, NULL) == 0);
}

/*
 * dwt_isr runs on a task on this port and the application configures the radio
 * from another one, so the decadriver's own decamutexon -- which only masks the
 * DW3110 interrupt line -- says nothing about the second caller. Two tasks
 * interleaving inside one transaction would splice their commands together on
 * the wire, and the chip would act on the splice.
 *
 * The lock is private to the backend, so what is checked is the balance: a take
 * with no matching give. On a lock guarding the only path to the radio that is
 * not a slow path, it is a wedge nothing recovers from, and the failure path is
 * where it hides.
 */
static void check_the_bus_lock_is_never_leaked(void)
{
	const uint8_t hdr[2] = {0x00, 0x00};
	unsigned takes;

	bring_up();
	bus_reset();

	takes = fake_semaphore_takes;
	CHECK("a transfer succeeds", dw3000_spi_write(2, hdr, 0, NULL) == 0);
	CHECK("a transfer takes the bus lock exactly once",
	      fake_semaphore_takes == takes + 1u);
	CHECK("a completed transfer releases the bus lock",
	      fake_semaphore_gives == fake_semaphore_takes);

	fake_spim.stall = true;
	CHECK("a stalled transfer fails", dw3000_spi_write(2, hdr, 0, NULL) == -1);
	CHECK("a failed transfer releases the bus lock too",
	      fake_semaphore_gives == fake_semaphore_takes);
	fake_spim.stall = false;

	CHECK("a refused transfer never took the lock at all",
	      (dw3000_spi_write(0, hdr, 0, NULL) == -1) &&
		      fake_semaphore_gives == fake_semaphore_takes);
}

/* ---- harness ------------------------------------------------------------ */

static void (*const g_scenarios[])(void) = {
	check_init,
	check_init_is_idempotent,
	check_write,
	check_write_crc,
	check_read,
	check_read_does_not_disturb_the_header,
	check_flash_body_is_copied,
	check_oversize_is_refused,
	check_stalled_transfer,
	check_speed_switch,
	check_wakeup,
	check_fini,
	check_the_bus_lock_is_never_leaked,
};

#define SCENARIO_COUNT ((int)(sizeof(g_scenarios) / sizeof(g_scenarios[0])))

static bool run_child(int scenario)
{
	pid_t pid;
	int status = 0;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		g_scenarios[scenario]();
		printf("RESULT-PART: %u checks\n", g_checks);
		fflush(stdout);
		_exit(g_failures == 0 ? 0 : 1);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		printf("  FAIL could not fork scenario %d\n", scenario);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	unsigned failures = 0;
	int scenario;

	for (scenario = 0; scenario < SCENARIO_COUNT; scenario++) {
		failures += run_child(scenario) ? 0u : 1u;
	}

	printf("dw3000-spi: %s (%d scenarios)\n", failures == 0 ? "PASS" : "FAIL", SCENARIO_COUNT);
	return failures == 0 ? 0 : 1;
}
