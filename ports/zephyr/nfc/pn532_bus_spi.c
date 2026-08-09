/* Zephyr SPI glue for the PN532 host protocol.
 *
 * PN532 SPI framing (UM0701-02 §6.2.5): every transaction opens with a
 * one-byte command — 0x01 DATAWRITE (host→PN532 frame), 0x02 STATREAD (read a
 * one-byte status; bit0 set = a response frame is ready), 0x03 DATAREAD
 * (PN532→host frame). The interface is byte-wise LSB-first, which the nRF5340
 * SPIM does in hardware (SPI_TRANSFER_LSB), so buffers hold ordinary MSB-order
 * bytes here and the peripheral flips them on the wire.
 *
 * Each command, status poll, and frame read is its own CS-cycled transaction
 * (the same shape as the Adafruit/ESPHome PN532 drivers). DATAREAD clocks its
 * command byte and the complete response through one contiguous SPIM transfer.
 * The chip re-presents the current frame on each DATAREAD, so reading more bytes
 * than a frame holds is harmless as long as CS is dropped between frames — with
 * one exception the caller enforces: the ACK read is kept short
 * (PN532_ACK_READ_LEN) because the response follows it immediately and a long
 * over-read would clock it away.
 *
 * Readiness is polled with STATREAD unless irq-gpios is wired (active low =
 * frame ready), in which case a GPIO edge wakes the waiting thread.
 */

#include <woz_nfc/pn532_bus.h>

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(woz_nfc_pn532_spi, CONFIG_WOZ_NFC_LOG_LEVEL);

#define DT_DRV_COMPAT nxp_pn532_spi

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "exactly one enabled nxp,pn532-spi devicetree node is required "
	     "(see apps/nrf5340dk-lock/overlays/pn532.overlay)");

/* SPI mode 0 (CPOL=0, CPHA=0), 8-bit words, LSB-first per the PN532 SPI spec. */
#define PN532_SPI_OP (SPI_OP_MODE_MASTER | SPI_TRANSFER_LSB | SPI_WORD_SET(8))

#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAREAD  0x03

#define PN532_READY_POLL_INTERVAL_MS 2

/* Cold-boot wake is deliberately separate from ordinary transaction timing:
 * NSS low starts the oscillator, but normal SPI transfers need no millisecond
 * setup/hold busy-wait. */
#define PN532_WAKEUP_HOLD_MS   10
#define PN532_WAKEUP_SETTLE_MS 10

/**
 * PN532 SPI driver state: SPI bus spec, optional IRQ GPIO, semaphore-gated interrupt, and
 * pre-allocated frame buffers for SPI read and write. The read_tx/read_rx buffers are kept in a
 * single descriptor to prevent the nrfx driver from splitting long PN532 responses.
 */
struct pn532_spi {
	struct spi_dt_spec bus;
	struct gpio_dt_spec irq;
	struct gpio_callback irq_callback;
	struct k_sem irq_sem;
	uint8_t write_tx[PN532_FRAME_BUF_SIZE + 1];
	/* Keep DATAREAD in one SPIM/EasyDMA descriptor.  A one-byte TX buffer
	 * paired with discard+payload RX buffers makes the nrfx driver split the
	 * transfer while CS is asserted; long PN532 responses have shown corrupted
	 * tails on that path. */
	uint8_t read_tx[PN532_FRAME_BUF_SIZE + 1];
	uint8_t read_rx[PN532_FRAME_BUF_SIZE + 1];
};

static struct pn532_spi ctx_spi = {
	.bus = SPI_DT_SPEC_INST_GET(0, PN532_SPI_OP),
	.irq = GPIO_DT_SPEC_INST_GET_OR(0, irq_gpios, {0}),
};

/**
 * GPIO interrupt handler for PN532 readiness. Fires when the IRQ line goes active and signals a
 * semaphore to wake the PN532 driver's polling task. Used only if an IRQ GPIO is configured in the
 * device tree.
 */
static void irq_ready(const struct device *port, struct gpio_callback *callback,
		      gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct pn532_spi *c = CONTAINER_OF(callback, struct pn532_spi, irq_callback);

	k_sem_give(&c->irq_sem);
}

/**
 * Read the PN532 SPI bus status register and return the device status byte.
 * Executes a PN532_SPI_STATREAD transaction on the configured SPI bus.
 * Returns PN532_OK on success and writes the status byte to *status; returns PN532_ERR_IO if the
 * SPI transceive fails.
 */
static int spi_status(struct pn532_spi *c, uint8_t *status)
{
	const uint8_t tx[2] = {PN532_SPI_STATREAD, 0x00};
	uint8_t rx[2] = {0};
	const struct spi_buf txb = {.buf = (void *)tx, .len = sizeof(tx)};
	const struct spi_buf rxb = {.buf = rx, .len = sizeof(rx)};
	const struct spi_buf_set txs = {.buffers = &txb, .count = 1};
	const struct spi_buf_set rxs = {.buffers = &rxb, .count = 1};

	if (spi_transceive_dt(&c->bus, &txs, &rxs) != 0) {
		return PN532_ERR_IO;
	}
	*status = rx[1];
	return PN532_OK;
}

/**
 * Write bytes to the PN532 NFC controller via SPI. Caller provides a buffer and length; this
 * function prepends the PN532 DATAWRITE command (0xD5) and transmits via the SPI bus. Returns
 * PN532_OK on success, PN532_ERR_IO if the buffer exceeds PN532_FRAME_BUF_SIZE or SPI write fails.
 */
static int bus_write(void *ctx, const uint8_t *buf, size_t len)
{
	struct pn532_spi *c = ctx;
	if (len > PN532_FRAME_BUF_SIZE) {
		return PN532_ERR_IO;
	}

	c->write_tx[0] = PN532_SPI_DATAWRITE;
	memcpy(c->write_tx + 1, buf, len);
	const struct spi_buf txb = {.buf = c->write_tx, .len = len + 1};
	const struct spi_buf_set txs = {.buffers = &txb, .count = 1};

	return spi_write_dt(&c->bus, &txs) == 0 ? PN532_OK : PN532_ERR_IO;
}

/**
 * Poll the PN532 SPI status register and return true if a response is ready (bit 0 set).
 */
static bool spi_is_response_ready(struct pn532_spi *c)
{
	uint8_t status = 0;

	if (spi_status(c, &status) != PN532_OK) {
		return false;
	}
	return (status & 0x01) != 0;
}

/**
 * Wait for the PN532 to be ready: poll the IRQ GPIO if configured (with semaphore fallback), else
 * poll SPI status. Timeout in milliseconds. Returns PN532_OK on ready, PN532_ERR_TIMEOUT on expiry,
 * PN532_ERR_IO on GPIO error.
 */
static int bus_wait_ready(void *ctx, int timeout_ms)
{
	struct pn532_spi *c = ctx;
	const int64_t deadline = k_uptime_get() + timeout_ms;

	if (c->irq.port != NULL) {
		for (;;) {
			const int value = gpio_pin_get_dt(&c->irq);

			if (value < 0) {
				return PN532_ERR_IO;
			}
			if (value != 0) { /* logical active; DT handles active-low polarity */
				return PN532_OK;
			}
			const int64_t remaining = deadline - k_uptime_get();

			if (remaining <= 0 || k_sem_take(&c->irq_sem, K_MSEC(remaining)) != 0) {
				return PN532_ERR_TIMEOUT;
			}
		}
	}

	do {
		if (spi_is_response_ready(c)) {
			return PN532_OK;
		}
		k_msleep(PN532_READY_POLL_INTERVAL_MS);
	} while (k_uptime_get() < deadline);

	return spi_is_response_ready(c) ? PN532_OK : PN532_ERR_TIMEOUT;
}

/* One contiguous DATAREAD transaction: send the command, clock out `cap` frame
 * bytes, and drop CS. The chip streams the current frame (00 00 FF …); the
 * parser in pn532.c locates the start code and validates the length while
 * tolerating trailing filler from an over-read. `cap` is the caller's read
 * budget, not a fixed size (see read_frame). */
static int bus_read(void *ctx, uint8_t *buf, size_t cap)
{
	struct pn532_spi *c = ctx;
	if (cap > PN532_FRAME_BUF_SIZE) {
		return PN532_ERR_IO;
	}

	const size_t transfer_len = cap + 1;
	memset(c->read_tx, 0, transfer_len);
	memset(c->read_rx, 0, transfer_len);
	c->read_tx[0] = PN532_SPI_DATAREAD;
	const struct spi_buf txb = {.buf = c->read_tx, .len = transfer_len};
	const struct spi_buf rxb = {.buf = c->read_rx, .len = transfer_len};
	const struct spi_buf_set txs = {.buffers = &txb, .count = 1};
	const struct spi_buf_set rxs = {.buffers = &rxb, .count = 1};

	if (spi_transceive_dt(&c->bus, &txs, &rxs) != 0) {
		return PN532_ERR_IO;
	}
	memcpy(buf, c->read_rx + 1, cap); /* discard command-phase MISO byte */
	LOG_HEXDUMP_DBG(buf, MIN(cap, (size_t)16), "PN532 rx");
	return PN532_OK;
}

const struct pn532_bus_ops pn532_bus_ops = {
	.write = bus_write,
	.wait_ready = bus_wait_ready,
	.read = bus_read,
};

/**
 * Return the SPI bus context for PN532 operations. Caller uses this opaque pointer to route I/O
 * callbacks to the correct PN532 device instance.
 */
void *pn532_bus_ctx(void)
{
	return &ctx_spi;
}

/**
 * Initialize the PN532 SPI bus, GPIO IRQ (if configured), and perform a cold-start wake pulse on
 * the chip-select line. Logs whether readiness is detected via IRQ or SPI polling. Returns 0 on
 * success, -1 if SPI/GPIO is not ready or interrupt setup fails. Must be called once before any
 * PN532 transactions.
 */
int pn532_bus_init(void)
{
	if (!spi_is_ready_dt(&ctx_spi.bus)) {
		LOG_ERR("PN532 SPI controller %s not ready (devicetree/driver issue, not wiring)",
			ctx_spi.bus.bus->name);
		return -1;
	}
	if (ctx_spi.irq.port != NULL) {
		if (!gpio_is_ready_dt(&ctx_spi.irq) ||
		    gpio_pin_configure_dt(&ctx_spi.irq, GPIO_INPUT) != 0) {
			LOG_ERR("PN532 IRQ GPIO (%s pin %d) configuration failed",
				ctx_spi.irq.port->name, ctx_spi.irq.pin);
			return -1;
		}
		k_sem_init(&ctx_spi.irq_sem, 0, 1);
		gpio_init_callback(&ctx_spi.irq_callback, irq_ready, BIT(ctx_spi.irq.pin));
		if (gpio_add_callback(ctx_spi.irq.port, &ctx_spi.irq_callback) != 0 ||
		    gpio_pin_interrupt_configure_dt(&ctx_spi.irq, GPIO_INT_EDGE_TO_ACTIVE) != 0) {
			LOG_ERR("PN532 IRQ GPIO (%s pin %d) interrupt setup failed",
				ctx_spi.irq.port->name, ctx_spi.irq.pin);
			return -1;
		}
		LOG_INF("PN532 IRQ on %s pin %d (active low); readiness via IRQ",
			ctx_spi.irq.port->name, ctx_spi.irq.pin);
	} else {
		LOG_INF("PN532 no IRQ line configured; readiness via SPI STATREAD polling");
	}
	/* Wake a cold PN532 without penalizing every later transaction. The DT CS
	 * GPIO is active-low, so logical 1 asserts it and logical 0 releases it. */
	if (!ctx_spi.bus.config.cs.cs_is_gpio ||
	    gpio_pin_configure_dt(&ctx_spi.bus.config.cs.gpio, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_set_dt(&ctx_spi.bus.config.cs.gpio, 1) != 0) {
		LOG_ERR("PN532 CS GPIO unavailable for cold-start wake pulse");
		return -1;
	}
	k_msleep(PN532_WAKEUP_HOLD_MS);
	if (gpio_pin_set_dt(&ctx_spi.bus.config.cs.gpio, 0) != 0) {
		LOG_ERR("PN532 CS GPIO failed to release after cold-start wake pulse");
		return -1;
	}
	k_msleep(PN532_WAKEUP_SETTLE_MS);
	return 0;
}
