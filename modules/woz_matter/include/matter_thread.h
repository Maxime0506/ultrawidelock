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

#ifdef __cplusplus
}
#endif
