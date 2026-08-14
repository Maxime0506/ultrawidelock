/**
 * @file matter_ble_freertos.h — the 0xFFF6 commissioning transport.
 *
 * Everything here is port-side glue. The protocol lives in modules/ultrawidelock_matter,
 * which knows nothing about BLE. The contract matches
 * ports/zephyr/matter/matter_ble_zephyr.h deliberately, so the commissioning
 * layer above can be shared between the two images.
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
 * Called on this transport's own task, never in the BLE RX callback, so a
 * handler may take its time. @p msg points into the reassembly area and is only
 * valid until the handler returns.
 */
typedef void (*matter_ble_msg_cb)(const uint8_t *msg, size_t len);

void matter_ble_set_msg_handler(matter_ble_msg_cb cb);

/**
 * The link went away and BTP state was thrown out.
 *
 * Anything layered on top has to forget its own state too: a commissioner that
 * drops mid-PASE and reconnects is starting over, and answering its first
 * message from halfway through the old exchange would fail in a way that looks
 * like a protocol bug rather than a dropped link.
 *
 * Called from the GAP event path, so do the cheap thing here and defer real work
 * to the next message.
 */
typedef void (*matter_ble_link_cb)(void);

void matter_ble_set_link_handler(matter_ble_link_cb cb);

/**
 * Register the 0xFFF6 service and start the transport's task.
 *
 * Call before ultrawidelock_freertos_nimble_host_start(): this adds a host hook, and the
 * service has to be registered inside the startup sequence rather than before
 * or after it.
 */
int matter_ble_start(void);

/**
 * Fragment and indicate a Matter message.
 * @return 0, or negative before the BTP handshake, when the peer has not
 *         subscribed to C2, or while another message is still going out.
 */
int matter_ble_send(const uint8_t *msg, size_t len);

/** UUID plus ChipBLEDeviceIdentificationInfo: what the service-data element carries. */
#define MATTER_BLE_SVC_DATA_LEN 10u

/**
 * Build the commissionable-node service data element.
 *
 * This does NOT start advertising, deliberately. The reader owns the single
 * advertising set, and it stays that way: a second set costs 24,844 B of flash
 * and 2,464 B of RAM measured on this board, and a Matter node advertises as
 * commissionable only while it has no fabric -- which is exactly when this
 * reader has nothing to advertise as a credential reader either. Flags 3 + Matter 12
 * + credential 26 is 41 bytes in a 31-byte legacy packet, so it is one or the other
 * regardless.
 *
 * @param out receives MATTER_BLE_SVC_DATA_LEN bytes, caller-owned.
 * @return 0 or negative.
 */
int matter_ble_commissionable_svc_data(uint8_t *out, size_t cap);

/** Override the advertised discriminator; 0 restores the built-in one. */
void matter_ble_set_discriminator(uint16_t discriminator);

/** The discriminator currently advertised. */
uint16_t matter_ble_discriminator(void);

#ifdef __cplusplus
}
#endif
