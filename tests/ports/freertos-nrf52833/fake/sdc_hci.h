/* Subset of the pinned nrfxlib sdc_hci.h. */
#ifndef TEST_SDC_HCI_H
#define TEST_SDC_HCI_H

#include <stdint.h>

typedef enum sdc_hci_msg_type {
	SDC_HCI_MSG_TYPE_NONE = 0x00,
	SDC_HCI_MSG_TYPE_DATA = 0x02,
	SDC_HCI_MSG_TYPE_EVT = 0x04,
	SDC_HCI_MSG_TYPE_ISO = 0x08,
} sdc_hci_msg_type_t;

int32_t sdc_hci_data_put(uint8_t const *p_data_in);
int32_t sdc_hci_get(uint8_t *p_packet_out, uint8_t *p_msg_type_out);

#endif /* TEST_SDC_HCI_H */
