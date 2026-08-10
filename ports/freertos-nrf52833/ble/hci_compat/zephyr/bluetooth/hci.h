/*
 * Zephyr-shaped shim, not Zephyr code.
 *
 * Only the Bluetooth Core HCI packet layout and the three status codes the
 * pinned Nordic opcode dispatcher uses. Every field below is fixed by the
 * Bluetooth Core specification, so this shim carries no Zephyr behavior; it
 * exists so the dispatcher compiles unmodified and can be re-pinned by a copy.
 */
#ifndef WOZ_HCI_COMPAT_BLUETOOTH_HCI_H
#define WOZ_HCI_COMPAT_BLUETOOTH_HCI_H

/* errno and string arrive transitively through Zephyr's own hci.h, and the
 * dispatcher uses memset and -EINVAL without including either itself. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "woz_hci_compat_util.h"

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

/* Core spec Vol 4, Part E, 5.4.1: OpCode Group Field, the top six bits. */
#define BT_OGF(opcode) (((opcode) >> 10) & BIT_MASK(6))
#define BT_OGF_LINK_CTRL 0x01
#define BT_OGF_BASEBAND 0x03
#define BT_OGF_INFO 0x04
#define BT_OGF_STATUS 0x05
#define BT_OGF_LE 0x08
#define BT_OGF_VS 0x3f

/* Core spec Vol 4, Part E, 5.4.1 HCI Command Packet */
struct bt_hci_cmd_hdr {
	uint16_t opcode;
	uint8_t param_len;
} __packed;
#define BT_HCI_CMD_HDR_SIZE 3

/* Core spec Vol 4, Part E, 5.4.4 HCI Event Packet */
struct bt_hci_evt_hdr {
	uint8_t evt;
	uint8_t len;
	uint8_t data[];
} __packed;
#define BT_HCI_EVT_HDR_SIZE 2

#define BT_HCI_EVT_CMD_COMPLETE 0x0e
struct bt_hci_evt_cmd_complete {
	uint8_t ncmd;
	uint16_t opcode;
} __packed;

struct bt_hci_evt_cc_status {
	uint8_t status;
} __packed;

#define BT_HCI_EVT_CMD_STATUS 0x0f
struct bt_hci_evt_cmd_status {
	uint8_t status;
	uint8_t ncmd;
	uint16_t opcode;
} __packed;

#define BT_HCI_ERR_SUCCESS 0x00
#define BT_HCI_ERR_UNKNOWN_CMD 0x01
#define BT_HCI_ERR_CMD_DISALLOWED 0x0c

#endif /* WOZ_HCI_COMPAT_BLUETOOTH_HCI_H */
