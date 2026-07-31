/**
 * @file matter_thread_port.c — matter_thread.h on top of Zephyr's OpenThread.
 *
 * The dataset arrives from the commissioner as raw meshcop TLVs and
 * otDatasetSetActiveTlvs() takes raw meshcop TLVs, so nothing here has to
 * understand the format -- which is the point. This node parses exactly one
 * field out of it, the Extended PAN ID, and only so it can name the network
 * back to the commissioner.
 *
 * Built into every image. Without CONFIG_NET_L2_OPENTHREAD it refuses honestly
 * rather than disappearing: matter_clusters.c calls it unconditionally, and a
 * link error would be a worse way to learn that Thread was configured out.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "matter_thread.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(matter_thread, CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL);

#if defined(CONFIG_NET_L2_OPENTHREAD)

#include <openthread.h>
#include <openthread/dataset.h>
#include <openthread/thread.h>

#include <string.h>

/** How often to look at the role while waiting. */
#define ATTACH_POLL_MS 250u

int matter_thread_start(const uint8_t *dataset, size_t len)
{
	otInstance *ot = openthread_get_default_instance();
	otOperationalDatasetTlvs tlvs;
	otError err;

	if (ot == NULL || dataset == NULL || len == 0u || len > sizeof(tlvs.mTlvs)) {
		return MATTER_E_INVAL;
	}

	memcpy(tlvs.mTlvs, dataset, len);
	tlvs.mLength = (uint8_t)len;

	openthread_mutex_lock();
	err = otDatasetSetActiveTlvs(ot, &tlvs);
	openthread_mutex_unlock();
	if (err != OT_ERROR_NONE) {
		LOG_ERR("dataset rejected by OpenThread (%d)", err);
		return MATTER_E_INVAL;
	}

	/*
	 * openthread_run() takes the mutex itself, so it must be called with the
	 * lock released. It enables IPv6 and then the Thread interface, which is
	 * when attaching actually begins.
	 */
	if (openthread_run() != 0) {
		LOG_ERR("OpenThread refused to start");
		return MATTER_E_STATE;
	}

	LOG_INF("OpenThread started on the commissioner's dataset (%u B)", (unsigned int)len);
	return MATTER_OK;
}

int matter_thread_wait_attached(uint32_t timeout_ms)
{
	uint32_t waited = 0u;

	for (;;) {
		otDeviceRole role;

		openthread_mutex_lock();
		role = otThreadGetDeviceRole(openthread_get_default_instance());
		openthread_mutex_unlock();

		if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
		    role == OT_DEVICE_ROLE_LEADER) {
			LOG_INF("Thread attached after %u ms, role %d", waited, (int)role);
			return MATTER_OK;
		}
		if (waited >= timeout_ms) {
			LOG_WRN("Thread still %s after %u ms",
				role == OT_DEVICE_ROLE_DETACHED ? "detached" : "disabled", waited);
			return MATTER_E_TIMEOUT;
		}

		k_msleep(ATTACH_POLL_MS);
		waited += ATTACH_POLL_MS;
	}
}

#else /* !CONFIG_NET_L2_OPENTHREAD */

int matter_thread_start(const uint8_t *dataset, size_t len)
{
	ARG_UNUSED(dataset);
	ARG_UNUSED(len);

	LOG_WRN("Thread dataset received, but this image has no Thread stack");
	return MATTER_E_STATE;
}

int matter_thread_wait_attached(uint32_t timeout_ms)
{
	ARG_UNUSED(timeout_ms);

	return MATTER_E_TIMEOUT;
}

#endif /* CONFIG_NET_L2_OPENTHREAD */
