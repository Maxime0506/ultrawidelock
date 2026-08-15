/* SPDX-License-Identifier: ISC */

/* DW3000-family wiring per ESP32 target, SPI2/FSPI. Source of truth for the
 * wiring table. Change to match how the UWB module is wired to your board. */
#ifndef ULTRAWIDELOCK_ESP_BOARD_PINS_H
#define ULTRAWIDELOCK_ESP_BOARD_PINS_H

#include "driver/spi_master.h"
#include "sdkconfig.h"

#define ULTRAWIDELOCK_DW3000_SPI_HOST   SPI2_HOST

#if CONFIG_IDF_TARGET_ESP32C5
/* ESP32-C5-DevKitC-1. The S3 data pins are taken on the C5: GPIO11/12 are the
 * UART0 console and GPIO13 is USB-Serial-JTAG. 8/9/23 avoid the strapping pins
 * (2/7/25/27/28, plus 3/26 per the devkit guide) and the GPIO27 RGB LED. */
#define ULTRAWIDELOCK_DW3000_PIN_SCLK    8
#define ULTRAWIDELOCK_DW3000_PIN_MOSI    9
#define ULTRAWIDELOCK_DW3000_PIN_MISO   23
#define ULTRAWIDELOCK_DW3000_PIN_CS     10
#define ULTRAWIDELOCK_DW3000_PIN_RST     4
#define ULTRAWIDELOCK_DW3000_PIN_IRQ     5
#define ULTRAWIDELOCK_DW3000_PIN_WAKEUP  6
#elif CONFIG_IDF_TARGET_ESP32C6
/* ESP32-C6-DevKitC-1. GPIO6/7/2 are SPI2's direct IO_MUX SCLK/MOSI/MISO
 * pins. The control pins avoid the C6 strapping pins (4/5/8/9/15), the
 * native USB Serial/JTAG pair (12/13), and UART0 (16/17). */
#define ULTRAWIDELOCK_DW3000_PIN_SCLK    6
#define ULTRAWIDELOCK_DW3000_PIN_MOSI    7
#define ULTRAWIDELOCK_DW3000_PIN_MISO    2
#define ULTRAWIDELOCK_DW3000_PIN_CS     10
#define ULTRAWIDELOCK_DW3000_PIN_RST     1
#define ULTRAWIDELOCK_DW3000_PIN_IRQ     3
#define ULTRAWIDELOCK_DW3000_PIN_WAKEUP  0
#elif CONFIG_IDF_TARGET_ESP32S3
/* ESP32-S3 (original bring-up target). */
#define ULTRAWIDELOCK_DW3000_PIN_SCLK   12
#define ULTRAWIDELOCK_DW3000_PIN_MOSI   11
#define ULTRAWIDELOCK_DW3000_PIN_MISO   13
#define ULTRAWIDELOCK_DW3000_PIN_CS     10
#define ULTRAWIDELOCK_DW3000_PIN_RST     4
#define ULTRAWIDELOCK_DW3000_PIN_IRQ     5
#define ULTRAWIDELOCK_DW3000_PIN_WAKEUP  6
#else
#error "Unsupported ESP32 target for the DW3000 port"
#endif

/* 2 MHz for init, 8 MHz steady (proven safe with a seated DWM3000EVB; matches
 * the nRF overlay). Raise once wiring is confirmed clean. */
#define ULTRAWIDELOCK_DW3000_SPI_SLOW_HZ 2000000
#define ULTRAWIDELOCK_DW3000_SPI_FAST_HZ 8000000

#endif /* ULTRAWIDELOCK_ESP_BOARD_PINS_H */
