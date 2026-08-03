/**
 * @file matter_thread.h — the seam between a commissioner's dataset and a radio.
 *
 * matter_clusters.c is platform-agnostic C11 and the host suite compiles it
 * without Zephyr, so it cannot call OpenThread. It calls these two instead; the
 * port forwards them to otDatasetSetActiveTlvs() and otThreadGetDeviceRole(),
 * and the host suite substitutes a double whose answers a test can choose.
 *
 * The split into start and wait is deliberate. Apple sends
 * AddOrUpdateThreadNetwork, then ArmFailSafe, then ConnectNetwork, and the
 * attach can begin at the first of those rather than the last -- a Thread
 * attach costs seconds and the round trips in between are free.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 7 of internal/cdk-matter-plan.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hand @p dataset to the Thread stack and start attaching.
 *
 * @p dataset is raw meshcop TLVs exactly as the commissioner sent them, which
 * is also exactly what otDatasetSetActiveTlvs() consumes -- nothing between
 * here and the radio has to understand the format.
 *
 * Returns as soon as the attach is under way; it does not wait for it.
 *
 * @return MATTER_OK if the stack accepted the dataset, MATTER_E_INVAL if it
 *         rejected it, MATTER_E_STATE if Thread could not be enabled.
 */
int matter_thread_start(const uint8_t *dataset, size_t len);

/**
 * Wait, bounded, for the node to attach.
 *
 * @param timeout_ms give up after this long. The caller is answering a
 *        commissioner that is blocked on the reply, so this must be shorter
 *        than the ConnectMaxTimeSeconds this node advertises.
 * @return MATTER_OK once attached (child, router or leader), MATTER_E_TIMEOUT
 *         if it never got there. A timeout is a real answer, not an error to
 *         paper over: the commissioner has to be told the node is not on the
 *         network rather than left to discover it by failing to find it.
 */
int matter_thread_wait_attached(uint32_t timeout_ms);

/**
 * The largest datagram this node sends.
 *
 * Set by the ReportData answering a subscription to everything, not by any
 * handshake message: a controller subscribes to the whole data model as soon as
 * it owns the node. Headroom over the report itself covers both headers and the
 * AEAD tag.
 *
 * It was 1664, sized from a 1479 B report measured BEFORE send_report_chunk()
 * existed. Chunking capped every outbound payload at MATTER_IM_PAYLOAD_MAX, so
 * headers + payload + tag can no longer exceed MATTER_MAX_MESSAGE_LEN, and the
 * 432 B above that were buying nothing this node can build. The value is that
 * ceiling; matter_commission.c BUILD_ASSERTs the identity, because the three
 * terms it is derived from live in headers this one does not include.
 *
 * Lowering it does not narrow what gets delivered, only what gets DIAGNOSED. A
 * message between this size and the old 1664 was framed, copied and sent, and
 * then dropped by the network for exceeding the MTU, with nothing logged. It
 * now trips the capacity check in send_framed() and says so.
 */
#define MATTER_THREAD_REPLY_MAX 1232u

/** The port a Matter node listens on operationally (lib/core/CHIPConfig.h:335). */
#define MATTER_OPERATIONAL_PORT 5540u

/**
 * Register this node's operational service so a commissioner can find it.
 *
 * Being ON the Thread network is not the same as being reachable: a
 * commissioner that finished network setup closes BLE and looks the node up in
 * DNS-SD, which on Thread means the border router answering on its behalf. It
 * only can if the node has told it, over SRP, that
 * "<instance>._matter._tcp.local" is at this address and port.
 *
 * @param instance_name "<compressed-fabric-id>-<node-id>", from
 *        matter_fabric_instance_name(). Borrowed for the length of the call.
 * @return MATTER_OK once the registration is under way -- the SRP server's
 *         answer arrives later and asynchronously -- or MATTER_E_STATE.
 */
int matter_thread_advertise(const char *instance_name, uint16_t port);

/**
 * Where a subscriber can be reached, kept opaque on purpose.
 *
 * A raw IPv6 address and port rather than an OpenThread type: this header is
 * the portable seam and the host suite builds it without any Thread stack.
 */
struct matter_thread_peer {
	uint8_t addr[16];
	uint16_t port;
	bool valid;
};

/**
 * Snapshot the peer of the datagram being processed right now.
 *
 * Only meaningful inside matter_thread_on_datagram(); outside it there is no
 * "current" datagram and @p out is marked invalid. Kept because a subscription
 * outlives the request that created it, and a report has to go somewhere.
 */
void matter_thread_peer_current(struct matter_thread_peer *out);

/**
 * Send one datagram to @p peer, outside any receive callback.
 *
 * The reply path returns its bytes to the caller and the transport sends them;
 * that cannot express a message this node originates, which is what a
 * subscription report is.
 *
 * @return MATTER_OK, or MATTER_E_STATE when the socket is down or @p peer was
 *         never captured.
 */
int matter_thread_send_to(const struct matter_thread_peer *peer, const uint8_t *msg, size_t len);

/**
 * Release every SRP registration this node holds.
 *
 * Call when the fabrics they name are discarded. A registration outlives the
 * fabric it advertises otherwise, and since the instance name is derived from
 * the fabric and node ids, a NEW commissioner never matches it -- so the table
 * fills with names for fabrics that no longer exist and the next commissioning
 * fails immediately after PASE with nothing to resolve.
 */
void matter_thread_advertise_reset(void);

/**
 * Handle one datagram that arrived on the operational port.
 *
 * Supplied by the application rather than called by it: the datagram arrives on
 * OpenThread's own thread, and the port has no business knowing what a Sigma1
 * is. The reply goes back through @p reply rather than being sent from inside,
 * for the same reason -- sending is the port's job and parsing is not.
 *
 * @return how many bytes of @p reply to send, or 0 for nothing to say.
 */
size_t matter_thread_on_datagram(const uint8_t *msg, size_t len, uint8_t *reply, size_t cap);

#ifdef __cplusplus
}
#endif
