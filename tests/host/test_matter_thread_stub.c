/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See test_matter_thread_stub.h.
 */
#include "test_matter_thread_stub.h"

#include <string.h>

#include "matter_thread.h"

uint8_t g_thread_last_dataset[512];
size_t g_thread_last_len;
int g_thread_start_calls;
int g_thread_wait_calls;
uint32_t g_thread_last_timeout_ms;
int g_thread_start_fail;
int g_thread_attached;

void test_matter_thread_stub_reset(void)
{
	memset(g_thread_last_dataset, 0, sizeof(g_thread_last_dataset));
	g_thread_last_len = 0u;
	g_thread_start_calls = 0;
	g_thread_wait_calls = 0;
	g_thread_last_timeout_ms = 0u;
	g_thread_start_fail = 0;
	g_thread_attached = 0;
}

int matter_thread_start(const uint8_t *dataset, size_t len)
{
	g_thread_start_calls++;
	if (dataset == NULL || len == 0u || len > sizeof(g_thread_last_dataset)) {
		return MATTER_E_INVAL;
	}
	/* Recorded even when the call is made to fail: what the cluster HANDED
	 * the stack is the thing worth asserting on either way. */
	memcpy(g_thread_last_dataset, dataset, len);
	g_thread_last_len = len;

	return g_thread_start_fail ? MATTER_E_STATE : MATTER_OK;
}

int matter_thread_wait_attached(uint32_t timeout_ms)
{
	g_thread_wait_calls++;
	g_thread_last_timeout_ms = timeout_ms;

	return g_thread_attached ? MATTER_OK : MATTER_E_TIMEOUT;
}
