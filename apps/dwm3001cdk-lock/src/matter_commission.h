/**
 * @file matter_commission.h — start answering commissioning attempts.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the commissioning handlers on the 0xFFF6 transport.
 *
 * Call after the reader is up. Nothing here touches the radio: whether the
 * board is discoverable as a commissionable node is decided by the advertising
 * branch in ultrawidelock_ble_zephyr.c, which asks for the Matter payload while this
 * node holds no fabric -- see matter_commission_has_fabric().
 *
 * @return 0. A bad verifier is reported by log and refused per attempt rather
 *         than failing startup -- a reader that cannot commission should still
 *         be a reader.
 */
int matter_commission_init(void);

/**
 * Whether this node currently holds a commissioned Matter fabric.
 *
 * Asked by the advertiser, which can carry the Matter commissionable
 * service data OR the Aliro reader tag but not both: flags 3 + Matter 12 +
 * Aliro 26 is 41 bytes in a 31-byte legacy packet, and a second advertising
 * set costs 24.8 KB of RAM.
 *
 * A device with no fabric MUST stay commissionable. Provisioning the reader
 * identity used to flip the advert to Aliro on its own, which left a board
 * that had just been provisioned -- and had lost its fabric to a failed
 * pairing -- invisible to Add Accessory and impossible to recover without
 * erasing it.
 */
bool matter_commission_has_fabric(void);

/**
 * True while an AdministratorCommissioning window is open.
 *
 * The advertiser needs this: a node that HAS a fabric normally advertises as
 * an Aliro reader, and doing that during a commissioning window hides it from
 * the very ecosystem the window was opened for.
 */
bool matter_commission_window_open(void);

#ifdef __cplusplus
}
#endif
