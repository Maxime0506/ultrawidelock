/*
 * What the nrf_802154 headers take from nrfx when the pinned OpenThread radio
 * platform is compiled on the host. The platform names no peripheral register
 * itself; the antenna diversity configuration merely carries a TIMER pointer,
 * so the type only has to exist.
 */
#ifndef TEST_OT_RADIO_NRFX_H
#define TEST_OT_RADIO_NRFX_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct nrf_timer_reg NRF_TIMER_Type;

#define __PACKED __attribute__((packed))
#define __ALIGN(n) __attribute__((aligned(n)))

#endif /* TEST_OT_RADIO_NRFX_H */
