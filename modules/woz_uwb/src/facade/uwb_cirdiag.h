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

/** @brief Arm or disarm the stream. Safe any time, even before the chip is probed: the
 * chip-side CIA logging enable happens lazily on the first armed reception. */
void uwb_cirdiag_set_enabled(bool on);

/** @brief Whether the stream is currently armed. */
bool uwb_cirdiag_enabled(void);

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
