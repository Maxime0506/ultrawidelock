/**
 * @file matter_commission.h — start answering commissioning attempts.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the commissioning handlers on the 0xFFF6 transport.
 *
 * Call after the reader is up. Nothing here touches the radio: whether the
 * board is discoverable as a commissionable node is decided by the advertising
 * branch in aliro_ble_zephyr.c, which asks for the Matter payload only while
 * the reader has no identity of its own.
 *
 * @return 0. A bad verifier is reported by log and refused per attempt rather
 *         than failing startup -- a reader that cannot commission should still
 *         be a reader.
 */
int matter_commission_init(void);

#ifdef __cplusplus
}
#endif
