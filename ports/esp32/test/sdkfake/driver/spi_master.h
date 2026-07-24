/* sdkfake driver/spi_master.h — recording SPI master (fake_driver.c). */
#ifndef SDKFAKE_DRIVER_SPI_MASTER_H
#define SDKFAKE_DRIVER_SPI_MASTER_H

#include "../esp_err.h"

#define SOC_SPI_MAXIMUM_BUFFER_SIZE 64u /* ESP32-S3 CPU-register burst cap */

typedef enum {
	SPI1_HOST = 0,
	SPI2_HOST,
	SPI3_HOST,
} spi_host_device_t;

#define SPI_DMA_DISABLED 0

typedef struct {
	int mosi_io_num;
	int miso_io_num;
	int sclk_io_num;
	int quadwp_io_num;
	int quadhd_io_num;
	int max_transfer_sz;
} spi_bus_config_t;

typedef struct {
	int mode;
	int spics_io_num;
	int queue_size;
	int clock_speed_hz;
} spi_device_interface_config_t;

typedef struct fake_spi_dev *spi_device_handle_t;
struct fake_spi_dev {
	spi_device_interface_config_t cfg;
};

typedef struct {
	size_t length; /* bits */
	const void *tx_buffer;
	void *rx_buffer;
} spi_transaction_t;

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *cfg, int dma);
esp_err_t spi_bus_free(spi_host_device_t host);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *cfg,
			     spi_device_handle_t *out);
esp_err_t spi_bus_remove_device(spi_device_handle_t dev);
esp_err_t spi_device_polling_transmit(spi_device_handle_t dev, spi_transaction_t *t);

/* ---- fake_driver.c control surface (SPI side) ---- */
struct fake_spi_txn {
	spi_device_handle_t dev;
	size_t bytes;
	int cs_level_during; /* CS GPIO level captured at transmit time */
	uint8_t tx[2100];
	int rx_requested;
};
#define FAKE_SPI_TXN_MAX 64
extern struct fake_spi_txn fake_spi_txns[FAKE_SPI_TXN_MAX];
extern int fake_spi_txn_count;
extern esp_err_t fake_spi_bus_init_rc;
extern esp_err_t fake_spi_add_device_rc_slow; /* first add */
extern esp_err_t fake_spi_add_device_rc_fast; /* second add */
extern esp_err_t fake_spi_transmit_rc;
extern int fake_spi_bus_inits, fake_spi_bus_frees, fake_spi_removed_devices;
extern spi_bus_config_t fake_spi_bus_cfg;
/* Bytes clocked back on MISO: copied into each transaction's rx_buffer at the
 * transaction's byte offset within the current CS window. */
extern uint8_t fake_spi_miso[2100];
/* Which GPIO the test considers the chip select, so each transaction records
 * the CS level it saw (framing assertion). -1 = don't capture. */
extern int fake_spi_cs_pin;
void fake_driver_reset(void);

#endif
