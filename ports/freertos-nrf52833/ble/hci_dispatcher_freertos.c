/*
 * Adapts Nordic's pinned HCI opcode dispatcher to this port's radio contract.
 *
 * The dispatcher itself (hci_internal.c, pinned in platform.lock.yml) is
 * compiled from the vendor tree unmodified. It resolves its handful of Zephyr
 * spellings through ble/hci_compat and its Kconfig symbols through
 * ble/hci_compat/woz_freertos_hci_config.h, so nothing Zephyr is linked. This
 * file owns only the two signature differences between the vendor API and
 * struct woz_freertos_radio_dispatcher.
 */
#include "woz_freertos_radio.h"

#include <stddef.h>

#include <sdc_hci.h>

/*
 * Declared here rather than by including the vendor hci_internal.h: that header
 * also drags in the Zephyr HCI driver typedef, which this port has no use for.
 */
extern int hci_internal_cmd_put(uint8_t *cmd_in);
extern int hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out);

static int32_t dispatcher_cmd_put(uint8_t *packet)
{
	return hci_internal_cmd_put(packet);
}

/*
 * The vendor API reports the message type as an enum. Narrowing it here keeps
 * the transport contract byte-sized and independent of whether the target
 * toolchain packs enums.
 */
static int32_t dispatcher_msg_get(uint8_t *packet, uint8_t *type)
{
	sdc_hci_msg_type_t msg_type = SDC_HCI_MSG_TYPE_NONE;
	int32_t rc;

	rc = hci_internal_msg_get(packet, &msg_type);
	if (rc == 0) {
		*type = (uint8_t)msg_type;
	}
	return rc;
}

const struct woz_freertos_radio_dispatcher *woz_freertos_radio_sdc_dispatcher(void)
{
	static const struct woz_freertos_radio_dispatcher dispatcher = {
		.cmd_put = dispatcher_cmd_put,
		.msg_get = dispatcher_msg_get,
	};

	return &dispatcher;
}
