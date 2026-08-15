/* SPDX-License-Identifier: ISC */

/** @file uwb_isr.h — DW3000 interrupt-callback registration (public surface). */

#ifndef ULTRAWIDELOCK_UWB_ISR_H_
#define ULTRAWIDELOCK_UWB_ISR_H_

/** @brief Install RX/TX callbacks and unmask the SYS_ENABLE bits; returns 0. */
int uwb_isr_register(void);

#endif /* ULTRAWIDELOCK_UWB_ISR_H_ */
