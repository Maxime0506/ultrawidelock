/*
 * Drive the pinned Nordic HCI opcode dispatcher through this port's compat
 * layer. The dispatcher is vendor code and is not under test; what is under
 * test is ports/freertos-nrf52833/ble/hci_compat - the packet layouts, the
 * OpCode Group Field split, the status codes, and the Kconfig selection the
 * dispatcher resolves through it - plus the adapter in
 * ble/hci_dispatcher_freertos.c.
 *
 * Every SoftDevice Controller command entry point is a generated recording
 * stub, so a command reaching the right controller call is observable.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nrf_errno.h>
#include <sdc_hci.h>
#include <sdc_hci_cmd_info_params.h>
#include <sdc_hci_cmd_le.h>

#include "ultrawidelock_freertos_radio.h"
#include "stub_sdc_hci_cmd.h"

/* Bluetooth Core spec Vol 4, Part E: opcodes and event layout, spelled out
 * here so a wrong value in the compat header cannot agree with itself. */
#define HCI_OPCODE_RESET 0x0c03
#define HCI_OPCODE_READ_LOCAL_VERSION 0x1001
#define HCI_OPCODE_LE_SET_ADV_ENABLE 0x200a
#define HCI_OPCODE_LE_SET_PHY 0x2032
#define HCI_OPCODE_UNKNOWN_LE 0x207f
#define HCI_OPCODE_UNKNOWN_VS 0xfc42
#define HCI_EVT_CMD_COMPLETE 0x0e
#define HCI_EVT_CMD_STATUS 0x0f
#define HCI_STATUS_SUCCESS 0x00
#define HCI_STATUS_UNKNOWN_CMD 0x01

static unsigned g_failures;
static unsigned g_checks;

#define CHECK(label, cond)                                                                         \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (cond) {                                                                        \
			printf("  ok   %s\n", (label));                                            \
		} else {                                                                           \
			printf("  FAIL %s\n", (label));                                            \
			g_failures++;                                                              \
		}                                                                                  \
	} while (0)

extern void hci_internal_supported_commands(sdc_hci_ip_supported_commands_t *cmds);

static void put_command(uint8_t *packet, uint16_t opcode, uint8_t param_len)
{
	packet[0] = (uint8_t)(opcode & 0xffu);
	packet[1] = (uint8_t)(opcode >> 8);
	packet[2] = param_len;
}

/* Reads back the Command Complete the dispatcher staged for the last command. */
static bool complete_for(const struct ultrawidelock_freertos_radio_dispatcher *d, uint16_t opcode,
			 uint8_t *status_out)
{
	uint8_t event[257];
	uint8_t type = 0xff;

	memset(event, 0, sizeof(event));
	if (d->msg_get(event, &type) != 0 || type != SDC_HCI_MSG_TYPE_EVT) {
		return false;
	}
	/* evt code, param len, num_hci_command_packets, opcode lo, opcode hi, status */
	if (event[0] != HCI_EVT_CMD_COMPLETE || event[1] < 4 || event[2] != 1) {
		return false;
	}
	if ((uint16_t)(event[3] | ((uint16_t)event[4] << 8)) != opcode) {
		return false;
	}
	*status_out = event[5];
	return true;
}

/*
 * An unhandled opcode always comes back as Command Status, whose parameters
 * are ordered status, num_hci_command_packets, opcode - the reverse of a
 * Command Complete. Reading it back proves the second packed layout in the
 * compat header independently of the first.
 */
static bool status_for(const struct ultrawidelock_freertos_radio_dispatcher *d, uint16_t opcode,
		       uint8_t *status_out)
{
	uint8_t event[257];
	uint8_t type = 0xff;

	memset(event, 0, sizeof(event));
	if (d->msg_get(event, &type) != 0 || type != SDC_HCI_MSG_TYPE_EVT) {
		return false;
	}
	if (event[0] != HCI_EVT_CMD_STATUS || event[1] != 4 || event[3] != 1) {
		return false;
	}
	if ((uint16_t)(event[4] | ((uint16_t)event[5] << 8)) != opcode) {
		return false;
	}
	*status_out = event[2];
	return true;
}

int main(void)
{
	const struct ultrawidelock_freertos_radio_dispatcher *d =
		ultrawidelock_freertos_radio_sdc_dispatcher();
	sdc_hci_ip_supported_commands_t cmds;
	uint8_t packet[260];
	uint8_t status = 0xff;
	uint8_t type = 0xff;

	memset(packet, 0, sizeof(packet));

	CHECK("the port publishes a complete dispatcher",
	      d != NULL && d->cmd_put != NULL && d->msg_get != NULL);

	CHECK("an idle dispatcher forwards the controller's no-data status",
	      d->msg_get(packet, &type) == -NRF_EAGAIN && ultrawidelock_stub_calls("sdc_hci_get") == 1);

	/* HCI Reset is Baseband OGF 0x03: it proves BT_OGF splits the opcode. */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_RESET, 0);
	CHECK("HCI Reset reaches the controller's reset command",
	      d->cmd_put(packet) == 0 && ultrawidelock_stub_calls("sdc_hci_cmd_cb_reset") == 1);
	CHECK("HCI Reset completes with success before the controller is polled",
	      complete_for(d, HCI_OPCODE_RESET, &status) && status == HCI_STATUS_SUCCESS &&
		      ultrawidelock_stub_calls("sdc_hci_get") == 0);

	/* Informational OGF 0x04, and the first command NimBLE sends after reset. */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_READ_LOCAL_VERSION, 0);
	CHECK("Read Local Version Information reaches the controller",
	      d->cmd_put(packet) == 0 &&
		      ultrawidelock_stub_calls("sdc_hci_cmd_ip_read_local_version_information") == 1);
	CHECK("Read Local Version Information returns its parameters in one event",
	      complete_for(d, HCI_OPCODE_READ_LOCAL_VERSION, &status) &&
		      status == HCI_STATUS_SUCCESS);

	/*
	 * LE Set PHY is answered with a Command Status even when it succeeds, so
	 * its status byte is zero while num_hci_command_packets is one. That is
	 * the only place the two Command Status fields hold different values, and
	 * therefore the only place their order is actually checkable.
	 */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_LE_SET_PHY, 7);
	CHECK("a successful LE Set PHY is reported as a Command Status",
	      d->cmd_put(packet) == 0 && ultrawidelock_stub_calls("sdc_hci_cmd_le_set_phy") == 1 &&
		      status_for(d, HCI_OPCODE_LE_SET_PHY, &status) &&
		      status == HCI_STATUS_SUCCESS);

	/* LE OGF 0x08, and the command that actually starts credential advertising. */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_LE_SET_ADV_ENABLE, 1);
	packet[3] = 1;
	CHECK("LE Set Advertising Enable reaches the controller's LE command",
	      d->cmd_put(packet) == 0 &&
		      ultrawidelock_stub_calls("sdc_hci_cmd_le_set_adv_enable") == 1 &&
		      complete_for(d, HCI_OPCODE_LE_SET_ADV_ENABLE, &status) &&
		      status == HCI_STATUS_SUCCESS);

	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_UNKNOWN_LE, 0);
	CHECK("an unsupported opcode is refused without touching the controller",
	      d->cmd_put(packet) == 0 && ultrawidelock_stub_total() == 0);
	CHECK("an unsupported opcode returns Unknown HCI Command as a status event",
	      status_for(d, HCI_OPCODE_UNKNOWN_LE, &status) &&
		      status == HCI_STATUS_UNKNOWN_CMD);

	/* OGF 0x3f is the vendor group. This port leaves CONFIG_BT_HCI_VS out, so
	 * reaching Unknown HCI Command here proves both that BT_OGF isolates the
	 * group and that the compat configuration really gates the command set. */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_UNKNOWN_VS, 0);
	CHECK("the vendor command group is not compiled into this port",
	      d->cmd_put(packet) == 0 && ultrawidelock_stub_total() == 0 &&
		      status_for(d, HCI_OPCODE_UNKNOWN_VS, &status) &&
		      status == HCI_STATUS_UNKNOWN_CMD);

	/* The dispatcher refuses a second command until the first one's event has
	 * been collected, which is why msg_get has to be the transport's read path. */
	ultrawidelock_stub_reset();
	put_command(packet, HCI_OPCODE_RESET, 0);
	CHECK("a command that is not yet answered blocks the next one",
	      d->cmd_put(packet) == 0 && d->cmd_put(packet) < 0 &&
		      ultrawidelock_stub_calls("sdc_hci_cmd_cb_reset") == 1);
	CHECK("collecting the event lets the next command through",
	      complete_for(d, HCI_OPCODE_RESET, &status) && d->cmd_put(packet) == 0 &&
		      ultrawidelock_stub_calls("sdc_hci_cmd_cb_reset") == 2);
	CHECK("the answered event is returned once and not replayed",
	      complete_for(d, HCI_OPCODE_RESET, &status) &&
		      d->msg_get(packet, &type) == -NRF_EAGAIN);

	memset(&cmds, 0, sizeof(cmds));
	hci_internal_supported_commands(&cmds);
	CHECK("the supported-command table advertises the linked peripheral features",
	      cmds.hci_reset && cmds.hci_le_set_advertising_data &&
		      cmds.hci_le_set_advertising_enable && cmds.hci_le_set_data_length &&
		      cmds.hci_le_set_phy && cmds.hci_read_rssi);
	CHECK("the supported-command table hides features this port does not link",
	      !cmds.hci_le_set_extended_advertising_data && !cmds.hci_le_set_scan_enable &&
		      !cmds.hci_le_create_connection &&
		      !cmds.hci_le_set_periodic_advertising_enable);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
