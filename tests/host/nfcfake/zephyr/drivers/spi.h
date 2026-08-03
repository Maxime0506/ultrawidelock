/* nfcfake: <zephyr/drivers/spi.h>. The buffer-set shape is Zephyr's, because
 * the driver builds descriptors rather than calling a byte-at-a-time API and
 * a suite asserts on what those descriptors carried. */
#ifndef NFCFAKE_ZEPHYR_DRIVERS_SPI_H
#define NFCFAKE_ZEPHYR_DRIVERS_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define SPI_OP_MODE_MASTER  (1u << 0)
#define SPI_TRANSFER_LSB    (1u << 4)
#define SPI_WORD_SET(bits)  ((uint16_t)(bits) << 5)

/** Chip-select description; the PN532 wake pulse drives this GPIO directly. */
struct spi_cs_control {
	bool cs_is_gpio;
	struct gpio_dt_spec gpio;
};

struct spi_config {
	uint32_t frequency;
	uint16_t operation;
	struct spi_cs_control cs;
};

struct spi_dt_spec {
	const struct device *bus;
	struct spi_config config;
};

struct spi_buf {
	void *buf;
	size_t len;
};

struct spi_buf_set {
	const struct spi_buf *buffers;
	size_t count;
};

/* The overlay wires CS to a GPIO, which is what the cold-start wake pulse
 * needs; a build where it is not a GPIO is refused by the driver. */
#define SPI_DT_SPEC_INST_GET(inst, op)                                                             \
	{                                                                                          \
		.bus = &nfcfake_spi_bus,                                                           \
		.config = {.frequency = 1000000u,                                                  \
			   .operation = (op),                                                      \
			   .cs = {.cs_is_gpio = true,                                              \
				  .gpio = {.port = &nfcfake_gpio_port, .pin = 26, .dt_flags = 0}}}  \
	}

/* C linkage: the driver is C and the fake that implements these is C++. */
#ifdef __cplusplus
extern "C" {
#endif

bool spi_is_ready_dt(const struct spi_dt_spec *spec);
int spi_transceive_dt(const struct spi_dt_spec *spec, const struct spi_buf_set *tx,
		      const struct spi_buf_set *rx);
int spi_write_dt(const struct spi_dt_spec *spec, const struct spi_buf_set *tx);

#ifdef __cplusplus
}
#endif

#endif /* NFCFAKE_ZEPHYR_DRIVERS_SPI_H */
