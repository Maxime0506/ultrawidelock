#include <string.h>

#include "fake_nimble.h"
#include <nimble/transport.h>

unsigned fake_nimble_event_alloc_calls;
int fake_nimble_last_event_discardable;
unsigned fake_nimble_event_alloc_failures;
unsigned fake_nimble_acl_alloc_calls;
unsigned fake_nimble_acl_alloc_failures;
unsigned fake_nimble_buffer_free_calls;
unsigned fake_nimble_mbuf_free_calls;
unsigned fake_nimble_host_event_calls;
unsigned fake_nimble_host_acl_calls;
uint8_t fake_nimble_host_packet[258];
size_t fake_nimble_host_packet_size;

static uint8_t fake_event[258];
static struct os_mbuf fake_acl;

void fake_nimble_reset(void)
{
	fake_nimble_event_alloc_calls = 0;
	fake_nimble_last_event_discardable = -1;
	fake_nimble_event_alloc_failures = 0;
	fake_nimble_acl_alloc_calls = 0;
	fake_nimble_acl_alloc_failures = 0;
	fake_nimble_buffer_free_calls = 0;
	fake_nimble_mbuf_free_calls = 0;
	fake_nimble_host_event_calls = 0;
	fake_nimble_host_acl_calls = 0;
	fake_nimble_host_packet_size = 0;
	memset(fake_nimble_host_packet, 0, sizeof(fake_nimble_host_packet));
	memset(fake_event, 0, sizeof(fake_event));
	memset(&fake_acl, 0, sizeof(fake_acl));
}

void fake_nimble_mbuf_set(struct os_mbuf *om, const uint8_t *data, size_t len)
{
	memset(om, 0, sizeof(*om));
	if (len <= sizeof(om->om_data)) {
		memcpy(om->om_data, data, len);
		om->om_len = (uint16_t)len;
	}
	om->om_packet_len = (uint16_t)len;
}

void *ble_transport_alloc_evt(int discardable)
{
	fake_nimble_event_alloc_calls++;
	fake_nimble_last_event_discardable = discardable;
	if (fake_nimble_event_alloc_failures != 0u) {
		fake_nimble_event_alloc_failures--;
		return NULL;
	}
	return fake_event;
}

struct os_mbuf *ble_transport_alloc_acl_from_ll(void)
{
	fake_nimble_acl_alloc_calls++;
	if (fake_nimble_acl_alloc_failures != 0u) {
		fake_nimble_acl_alloc_failures--;
		return NULL;
	}
	memset(&fake_acl, 0, sizeof(fake_acl));
	return &fake_acl;
}

void ble_transport_free(void *buf)
{
	(void)buf;
	fake_nimble_buffer_free_calls++;
}

int ble_transport_to_hs_evt(void *buf)
{
	const uint8_t *event = buf;
	size_t size = 2u + event[1];

	fake_nimble_host_event_calls++;
	fake_nimble_host_packet_size = size;
	memcpy(fake_nimble_host_packet, event, size);
	return 0;
}

int ble_transport_to_hs_acl(struct os_mbuf *om)
{
	fake_nimble_host_acl_calls++;
	fake_nimble_host_packet_size = om->om_packet_len;
	memcpy(fake_nimble_host_packet, om->om_data, om->om_packet_len);
	return 0;
}

int os_mbuf_copydata(const struct os_mbuf *om, int off, int len, void *dst)
{
	if (om == NULL || dst == NULL || off < 0 || len < 0 ||
	    (size_t)off + (size_t)len > om->om_packet_len) {
		return -1;
	}
	memcpy(dst, om->om_data + off, (size_t)len);
	return 0;
}

int os_mbuf_append(struct os_mbuf *om, const void *data, uint16_t len)
{
	if (om == NULL || data == NULL || (size_t)om->om_len + len > sizeof(om->om_data)) {
		return -1;
	}
	memcpy(om->om_data + om->om_len, data, len);
	om->om_len += len;
	om->om_packet_len += len;
	return 0;
}

int os_mbuf_free_chain(struct os_mbuf *om)
{
	(void)om;
	fake_nimble_mbuf_free_calls++;
	return 0;
}
