#ifndef TEST_FAKE_NIMBLE_H
#define TEST_FAKE_NIMBLE_H

#include <stddef.h>
#include <stdint.h>

#include <os/os_mbuf.h>

extern unsigned fake_nimble_event_alloc_calls;
extern int fake_nimble_last_event_discardable;
extern unsigned fake_nimble_event_alloc_failures;
extern unsigned fake_nimble_acl_alloc_calls;
extern unsigned fake_nimble_acl_alloc_failures;
extern unsigned fake_nimble_buffer_free_calls;
extern unsigned fake_nimble_mbuf_free_calls;
extern unsigned fake_nimble_host_event_calls;
extern unsigned fake_nimble_host_acl_calls;
extern uint8_t fake_nimble_host_packet[258];
extern size_t fake_nimble_host_packet_size;

void fake_nimble_reset(void);
void fake_nimble_mbuf_set(struct os_mbuf *om, const uint8_t *data, size_t len);

#endif /* TEST_FAKE_NIMBLE_H */
