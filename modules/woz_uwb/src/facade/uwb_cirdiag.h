/** @file uwb_cirdiag.h — Per-reception CIA first-path/STS diagnostics stream (channel-impulse
 * Stage 0). The RX callback latches the DW3000's CIA diagnostic bank (Ipatov/STS first-path
 * index, F1..F3, power, peak, STS quality, xtal offset); task context emits it as one
 * "[ALAB] t=<us> ev=uwb.diag ..." line for tools/aliro_lab.py. OFF at boot; armed at runtime
 * (nRF `aliro cir on`, ESP32 rides the `lab on` gate). */

#ifndef WOZ_UWB_CIRDIAG_H_
#define WOZ_UWB_CIRDIAG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Arm or disarm the summary stream. Safe any time, even before the chip is probed: the
 * chip-side CIA logging enable happens lazily on the first armed reception. */
void uwb_cirdiag_set_enabled(bool on);

/** @brief Whether the summary stream is currently armed. */
bool uwb_cirdiag_enabled(void);

/** @brief Arm or disarm the windowed-CIR dump (Stage 1): while armed, each reception's ~64-tap
 * Ipatov window around the first path is buffered to RAM — NOT printed, because printing on the
 * RX path stalls ranging. Disarming drains the buffer to `ev=uwb.cir` lines off the ranging hot
 * path, so a live walk-up still unlocks while capturing. Arming implies the summary stream too;
 * disarming leaves it. Costs one CIR SPI read per reception + a burst of serial lines on disarm.
 * Safe any time (the read only runs inside a live armed reception). */
void uwb_cirdiag_dump_set_enabled(bool on);

/** @brief Whether the windowed-CIR dump is currently armed. */
bool uwb_cirdiag_dump_enabled(void);

/** @brief Number of CIR windows currently buffered awaiting a drain (0..CIRDIAG_RING_RECS).
 * The drain on dump disarm empties it back to 0. Exposed mainly for tests. */
uint32_t uwb_cirdiag_ring_count(void);

/**
 * @brief Latch the CIA diagnostics of the reception just serviced (latest wins).
 * Call from the RX-good callback, after the re-arm/decode work — it costs one ~220-byte SPI
 * read. No-op unless armed.
 * @param status RX callback status word (0 if the callback data was NULL).
 * @param datalength RX frame length (0 if the callback data was NULL).
 * @return true if a snapshot was latched (caller should schedule a flush).
 */
bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength);

/** @brief Emit the latched snapshot as one [ALAB] line, if any is pending. Task context only —
 * never call on the RX event path. */
void uwb_cirdiag_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* WOZ_UWB_CIRDIAG_H_ */
