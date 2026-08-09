#ifndef TEST_NIMBLE_TRANSPORT_H
#define TEST_NIMBLE_TRANSPORT_H

struct os_mbuf;

void *ble_transport_alloc_evt(int discardable);
struct os_mbuf *ble_transport_alloc_acl_from_ll(void);
void ble_transport_free(void *buf);
int ble_transport_to_hs_evt(void *buf);
int ble_transport_to_hs_acl(struct os_mbuf *om);

void ble_transport_ll_init(void);
int ble_transport_to_ll_cmd_impl(void *buf);
int ble_transport_to_ll_acl_impl(struct os_mbuf *om);
int ble_transport_to_ll_iso_impl(struct os_mbuf *om);

#endif /* TEST_NIMBLE_TRANSPORT_H */
