#ifndef TEST_OS_MBUF_H
#define TEST_OS_MBUF_H

#include <stdint.h>

struct os_mbuf {
	uint8_t om_data[258];
	uint16_t om_len;
	uint16_t om_packet_len;
};

#define OS_MBUF_PKTLEN(om) ((om)->om_packet_len)

int os_mbuf_copydata(const struct os_mbuf *om, int off, int len, void *dst);
int os_mbuf_append(struct os_mbuf *om, const void *data, uint16_t len);
int os_mbuf_free_chain(struct os_mbuf *om);

#endif /* TEST_OS_MBUF_H */
