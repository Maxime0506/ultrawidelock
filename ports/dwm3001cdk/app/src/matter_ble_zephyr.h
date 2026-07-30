/**
 * @file matter_ble_zephyr.h — the 0xFFF6 commissioning transport.
 *
 * Everything here is Zephyr-side glue. The protocol lives in
 * modules/woz_matter, which knows nothing about BLE.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A fully reassembled Matter message.
 *
 * Called on the node's own work queue, never in the BLE RX callback, so a
 * handler may take its time. @p msg points into the reassembly area and is
 * only valid until the handler returns.
 */
typedef void (*matter_ble_msg_cb)(const uint8_t *msg, size_t len);

/*
 * There is no init call. The work queue is started by SYS_INIT at APPLICATION
 * priority, because the GATT service is registered statically and the two must
 * come up together.
 */
void matter_ble_set_msg_handler(matter_ble_msg_cb cb);

/**
 * Fragment and indicate a Matter message.
 * @return 0, -ENOTCONN before the BTP handshake, -EAGAIN if the peer has not
 *         subscribed to C2, -EBUSY while another message is still going out.
 */
int matter_ble_send(const uint8_t *msg, size_t len);

/**
 * Advertise as commissionable.
 *
 * NOT called by the reader: it owns the advertising set and advertises Aliro
 * 0xFFF2. Running both needs extended advertising with two sets or a
 * deliberate hand-off, which is an open decision.
 */
int matter_ble_advertise_start(uint16_t discriminator, uint16_t vendor_id, uint16_t product_id);
int matter_ble_advertise_stop(void);

#ifdef __cplusplus
}
#endif
