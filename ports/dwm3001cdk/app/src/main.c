/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * DWM3001CDK standalone Aliro reader.
 *
 * One board: the nRF52833 runs the BLE peripheral and the Aliro reader engine,
 * and the DW3110 in the same DWM3001C module does the UWB ranging. No host MCU
 * board, no seated DWM3000EVB, no NFC (the CDK has none).
 *
 * Stage 0 keeps main deliberately thin. Its job is to hold the whole call graph
 * live so the linker cannot garbage-collect the engine and hand back a size
 * number that flatters us.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aliro_reader.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Reader status housekeeping: the engine expects a periodic tick to age out a
 * stalled transaction and to drive the ranging power gate's decay. 250 ms is
 * the cadence the ESP32 port runs. */
#define ALIRO_TICK_MS 250

int main(void)
{
	/* ASCII only: the console is a byte stream, and a UTF-8 dash renders as
	 * mojibake in RTT Viewer. */
	LOG_INF("openaliro reader: DWM3001CDK (nRF52833 + DW3110)");

	int rc = aliro_reader_start();

	if (rc != 0) {
		LOG_ERR("aliro_reader_start rc=%d", rc);
		return rc;
	}

	while (1) {
		aliro_reader_status_tick(k_uptime_get());
		k_msleep(ALIRO_TICK_MS);
	}
	return 0;
}
