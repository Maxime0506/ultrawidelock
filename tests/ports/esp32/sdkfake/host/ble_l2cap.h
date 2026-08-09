/* sdkfake host/ble_l2cap.h — L2CAP CoC slice used by aliro_ble.c. */
#ifndef SDKFAKE_HOST_BLE_L2CAP_H
#define SDKFAKE_HOST_BLE_L2CAP_H

#include "ble_hs.h"

#define BLE_L2CAP_EVENT_COC_CONNECTED     0
#define BLE_L2CAP_EVENT_COC_DISCONNECTED  1
#define BLE_L2CAP_EVENT_COC_ACCEPT        2
#define BLE_L2CAP_EVENT_COC_DATA_RECEIVED 3

struct ble_l2cap_chan; /* opaque; the fake hands out distinct pointers */

struct ble_l2cap_event {
	uint8_t type;
	union {
		struct {
			struct ble_l2cap_chan *chan;
		} accept;
		struct {
			int status;
			uint16_t conn_handle;
			struct ble_l2cap_chan *chan;
		} connect;
		struct {
			uint16_t conn_handle;
			struct ble_l2cap_chan *chan;
		} disconnect;
		struct {
			uint16_t conn_handle;
			struct ble_l2cap_chan *chan;
			struct os_mbuf *sdu_rx;
		} receive;
	};
};

typedef int (*ble_l2cap_event_fn)(struct ble_l2cap_event *event, void *arg);

int ble_l2cap_create_server(uint16_t psm, uint16_t mtu, ble_l2cap_event_fn cb, void *arg);
int ble_l2cap_recv_ready(struct ble_l2cap_chan *chan, struct os_mbuf *sdu_rx);
int ble_l2cap_send(struct ble_l2cap_chan *chan, struct os_mbuf *sdu_tx);

/* ---- fake_nimble.c control surface ---- */
extern int fake_l2cap_create_server_rc;
extern uint16_t fake_l2cap_server_psm, fake_l2cap_server_mtu;
extern ble_l2cap_event_fn fake_l2cap_event_cb;
extern void *fake_l2cap_event_arg;
extern int fake_l2cap_recv_ready_calls;
extern struct os_mbuf *fake_l2cap_last_rx_sdu;
extern int fake_l2cap_send_rc;      /* 0 / BLE_HS_ESTALLED / other */
extern uint8_t fake_l2cap_sent[600];
extern size_t fake_l2cap_sent_len;
extern int fake_l2cap_send_calls;

#endif
