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

#ifdef __cplusplus
}
#endif
