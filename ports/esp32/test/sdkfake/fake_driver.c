/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * fake_driver — recording doubles of the ESP-IDF GPIO and SPI-master APIs for
 * the dw3000_hw/dw3000_spi host suite. GPIO writes land in plain arrays; SPI
 * transactions are copied out whole so the test can assert the exact framing
 * (header/body/crc bytes, chunk lengths, CS level during each burst). MISO
 * bytes come from fake_spi_miso at the transaction's offset inside the current
 * CS window (detected by tx-buffer contiguity, matching dw_xfer's chunking).
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"

uint32_t fake_cpu_cycles;
uint32_t fake_rom_delay_us_total;
int fake_rom_delay_calls;

gpio_mode_t fake_gpio_mode[FAKE_GPIO_MAX];
gpio_int_type_t fake_gpio_intr[FAKE_GPIO_MAX];
uint32_t fake_gpio_level[FAKE_GPIO_MAX];
uint32_t fake_gpio_input_level[FAKE_GPIO_MAX];
int fake_gpio_intr_enabled[FAKE_GPIO_MAX];
esp_err_t fake_gpio_isr_service_rc = ESP_OK;
int fake_gpio_isr_service_installs;
void (*fake_gpio_isr[FAKE_GPIO_MAX])(void *);
void *fake_gpio_isr_arg[FAKE_GPIO_MAX];
void (*fake_gpio_get_level_hook)(int pin);

struct fake_spi_txn fake_spi_txns[FAKE_SPI_TXN_MAX];
int fake_spi_txn_count;
esp_err_t fake_spi_bus_init_rc = ESP_OK;
esp_err_t fake_spi_add_device_rc_slow = ESP_OK;
esp_err_t fake_spi_add_device_rc_fast = ESP_OK;
esp_err_t fake_spi_transmit_rc = ESP_OK;
int fake_spi_bus_inits, fake_spi_bus_frees, fake_spi_removed_devices;
spi_bus_config_t fake_spi_bus_cfg;
uint8_t fake_spi_miso[2100];
int fake_spi_cs_pin = -1;

static struct fake_spi_dev s_devs[4];
static int s_dev_count;
static int s_adds;
static const uint8_t *s_prev_tx_end; /* CS-window contiguity tracker */
static size_t s_window_off;

void fake_driver_reset(void)
{
	memset(fake_gpio_mode, 0, sizeof(fake_gpio_mode));
	memset(fake_gpio_intr, 0, sizeof(fake_gpio_intr));
	memset(fake_gpio_level, 0, sizeof(fake_gpio_level));
	memset(fake_gpio_input_level, 0, sizeof(fake_gpio_input_level));
	memset(fake_gpio_intr_enabled, 0, sizeof(fake_gpio_intr_enabled));
	memset(fake_gpio_isr, 0, sizeof(fake_gpio_isr));
	memset(fake_gpio_isr_arg, 0, sizeof(fake_gpio_isr_arg));
	fake_gpio_isr_service_rc = ESP_OK;
	fake_gpio_isr_service_installs = 0;
	fake_gpio_get_level_hook = NULL;
	memset(fake_spi_txns, 0, sizeof(fake_spi_txns));
	fake_spi_txn_count = 0;
	fake_spi_bus_init_rc = ESP_OK;
	fake_spi_add_device_rc_slow = ESP_OK;
	fake_spi_add_device_rc_fast = ESP_OK;
	fake_spi_transmit_rc = ESP_OK;
	fake_spi_bus_inits = 0;
	fake_spi_bus_frees = 0;
	fake_spi_removed_devices = 0;
	memset(&fake_spi_bus_cfg, 0, sizeof(fake_spi_bus_cfg));
	memset(fake_spi_miso, 0, sizeof(fake_spi_miso));
	fake_spi_cs_pin = -1;
	memset(s_devs, 0, sizeof(s_devs));
	s_dev_count = 0;
	s_adds = 0;
	s_prev_tx_end = NULL;
	s_window_off = 0;
	fake_rom_delay_us_total = 0;
	fake_rom_delay_calls = 0;
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
	for (int pin = 0; pin < FAKE_GPIO_MAX; pin++) {
		if (cfg->pin_bit_mask & (1ULL << pin)) {
			fake_gpio_mode[pin] = cfg->mode;
			fake_gpio_intr[pin] = cfg->intr_type;
		}
	}
	return ESP_OK;
}

esp_err_t gpio_set_level(int pin, uint32_t level)
{
	fake_gpio_level[pin] = level;
	return ESP_OK;
}

int gpio_get_level(int pin)
{
	int lvl = (int)fake_gpio_input_level[pin];

	if (fake_gpio_get_level_hook != NULL) {
		fake_gpio_get_level_hook(pin);
	}
	return lvl;
}

esp_err_t gpio_set_direction(int pin, gpio_mode_t mode)
{
	fake_gpio_mode[pin] = mode;
	return ESP_OK;
}

esp_err_t gpio_install_isr_service(int flags)
{
	(void)flags;
	fake_gpio_isr_service_installs++;
	return fake_gpio_isr_service_rc;
}

esp_err_t gpio_isr_handler_add(int pin, void (*isr)(void *), void *arg)
{
	fake_gpio_isr[pin] = isr;
	fake_gpio_isr_arg[pin] = arg;
	return ESP_OK;
}

esp_err_t gpio_intr_enable(int pin)
{
	fake_gpio_intr_enabled[pin] = 1;
	return ESP_OK;
}

esp_err_t gpio_intr_disable(int pin)
{
	fake_gpio_intr_enabled[pin] = 0;
	return ESP_OK;
}

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *cfg, int dma)
{
	(void)host;
	(void)dma;
	fake_spi_bus_inits++;
	fake_spi_bus_cfg = *cfg;
	return fake_spi_bus_init_rc;
}

esp_err_t spi_bus_free(spi_host_device_t host)
{
	(void)host;
	fake_spi_bus_frees++;
	return ESP_OK;
}

esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *cfg,
			     spi_device_handle_t *out)
{
	(void)host;

	esp_err_t rc = (s_adds == 0) ? fake_spi_add_device_rc_slow : fake_spi_add_device_rc_fast;

	s_adds++;
	if (rc != ESP_OK) {
		return rc;
	}
	if (s_dev_count >= (int)(sizeof(s_devs) / sizeof(s_devs[0]))) {
		return ESP_FAIL;
	}
	s_devs[s_dev_count].cfg = *cfg;
	*out = &s_devs[s_dev_count++];
	return ESP_OK;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t dev)
{
	(void)dev;
	fake_spi_removed_devices++;
	return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t dev, spi_transaction_t *t)
{
	if (fake_spi_transmit_rc != ESP_OK) {
		return fake_spi_transmit_rc;
	}

	size_t bytes = t->length / 8u;

	/* Contiguous tx pointers = same CS window (dw_xfer chunking); else new. */
	if (t->tx_buffer != s_prev_tx_end) {
		s_window_off = 0;
	}

	if (fake_spi_txn_count < FAKE_SPI_TXN_MAX) {
		struct fake_spi_txn *rec = &fake_spi_txns[fake_spi_txn_count++];

		rec->dev = dev;
		rec->bytes = bytes;
		rec->rx_requested = (t->rx_buffer != NULL);
		rec->cs_level_during =
			(fake_spi_cs_pin >= 0) ? (int)fake_gpio_level[fake_spi_cs_pin] : -1;
		if (bytes <= sizeof(rec->tx)) {
			memcpy(rec->tx, t->tx_buffer, bytes);
		}
	}

	if (t->rx_buffer != NULL && s_window_off + bytes <= sizeof(fake_spi_miso)) {
		memcpy(t->rx_buffer, fake_spi_miso + s_window_off, bytes);
	}

	s_prev_tx_end = (const uint8_t *)t->tx_buffer + bytes;
	s_window_off += bytes;
	return ESP_OK;
}
