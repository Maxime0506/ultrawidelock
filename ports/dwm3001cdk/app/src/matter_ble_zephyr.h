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

/** UUID plus ChipBLEDeviceIdentificationInfo: what BT_DATA_SVC_DATA16 carries. */
#define MATTER_BLE_SVC_DATA_LEN 10u

/**
 * Build the commissionable-node service data element.
 *
 * This does NOT start advertising, deliberately. The reader owns the single
 * advertising set (aliro_ble_zephyr.c), and it stays that way: Zephyr's legacy
 * bt_le_adv_start API has exactly one set, and the alternative -- CONFIG_BT_EXT_ADV
 * with two sets -- measured +24,844 B of flash and +2,464 B of RAM on this board
 * before either advertiser was rewritten to use it. On a part where CASE and the
 * Interaction Model are still unbuilt, that is not a trade worth making for
 * something the protocol does not ask for: a Matter node advertises as
 * commissionable only while it has no fabric, which is exactly when this reader
 * has nothing to advertise as an Aliro reader either.
 *
 * So the reader asks for these bytes when it has no identity yet, and stops
 * asking once it has one.
 *
 * @param out receives MATTER_BLE_SVC_DATA_LEN bytes, caller-owned and required to
 *        outlive the advertisement -- bt_data holds the pointer, not a copy.
 * @return 0 or -EINVAL.
 */
int matter_ble_commissionable_svc_data(uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
