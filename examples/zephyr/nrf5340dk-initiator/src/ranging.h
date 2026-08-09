// Device-side UWB ranging-setup driver for the initiator: consumes the reader's
// BleSK-sealed post-auth SDUs and answers them, walking AP-Completed ->
// Initiate-Ranging-Session -> M1 -> M2 -> M3 -> M4.
/*
 * The device mirror of modules/woz_aliro/src/aliro_ranging.c, which is
 * reader-only. Everything below the transport is existing code: the BleSK
 * channel is aliro_device.c's sc_ble and the M1-M4 codec is
 * modules/woz_uwb/src/aliro/aliro_device_uwb.c. This file is the glue that was
 * missing, and it lives in the application rather than in modules/ because the
 * radio half is only one frame deep: a device "ranging session" here is a BLE
 * conversation plus a Pre-POLL, and calling it aliro_device_ranging in modules/
 * would promise the rest of the block.
 *
 * SCOPE, stated plainly: this reaches the end of ranging SETUP and then hands
 * off to prepoll_tx.c, which sends the Pre-POLL that opens each ranging block.
 * The four frames after it -- POLL, RESPONSE, Final, Final_Data -- do not exist
 * on this board, so no distance is ever measured.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct aliro_device;

/** Arm the ranging-setup driver for @conn, once the Access Protocol has reached
 *  ESTABLISHED and @dev->sc_ble is therefore keyed. Idempotent per connection. */
void initiator_ranging_begin(struct aliro_device *dev, uint16_t conn);

/** Feed one inbound post-auth SDU, still sealed, exactly as it came off L2CAP
 *  (wire = [proto][id][len_be16][ct||tag]). Opens it on the BleSK channel and
 *  answers if the message calls for an answer. Returns 0 if the SDU was consumed,
 *  <0 if it was not ours or could not be opened. */
int initiator_ranging_on_sdu(uint16_t conn, const uint8_t *wire, size_t wire_len);

/** Drop the ranging state for @conn. Safe to call for a connection that never
 *  reached ESTABLISHED. */
void initiator_ranging_end(uint16_t conn);
