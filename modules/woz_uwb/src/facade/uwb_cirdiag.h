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

/* No-op inlines when the feature is compiled out, so the latch call sites in the two RX shims
 * and the console commands cost nothing in a build that does not want the diagnostics. */
#if defined(ESP_PLATFORM)
#include "sdkconfig.h" /* CONFIG_WOZ_UWB_CIRDIAG (Zephyr injects autoconf.h itself) */
#endif

#if defined(CONFIG_WOZ_UWB_CIRDIAG)

/** @brief Arm or disarm the summary stream. Safe any time, even before the chip is probed: the
 * chip-side CIA logging enable happens lazily on the first armed reception. */
void uwb_cirdiag_set_enabled(bool on);

/** @brief Whether the summary stream is currently armed. */
bool uwb_cirdiag_enabled(void);

/** @brief Arm or disarm the windowed-CIR dump (Stage 1): while armed, a ~64-tap Ipatov window
 * around the first path is buffered to RAM — NOT printed, because printing on the RX path stalls
 * ranging. Disarming drains the buffer to `ev=uwb.cir` lines off the ranging hot path, so a live
 * walk-up still unlocks while capturing. Arming implies the summary stream too; disarming leaves
 * it. Costs one CIR SPI read per non-POLL reception (see uwb_cirdiag_capture) + a burst of serial
 * lines on disarm. Safe any time (the read only runs inside a live armed reception). */
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
 * @param deadline_pending The responder still owes this ranging block a radio event — pass
 * ccc_shim_rx_deadline_pending() sampled after the blob's RX handler has re-armed. True on the
 * Pre-POLL (POLL RX armed) and on the POLL (Final RX armed ~2 ms out); false only on the Final,
 * which has the whole ~192 ms inter-block gap behind it. The summary read is taken either way
 * (bench-proven harmless), but the far larger windowed-CIR read happens only when this is false:
 * it costs ~260 us of SPI plus dwt_readcir's own ACC clock forcing, and an armed dump inside a
 * live block cost every range of the walk-up on the bench.
 * @return true if a snapshot was latched (caller should schedule a flush).
 */
bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending);

/** @brief Emit the latched snapshot as one [ALAB] line, if any is pending. Task context only —
 * never call on the RX event path. */
void uwb_cirdiag_flush(void);

/** @brief Whether this ranging block should take a windowed-CIR read. Call once per Final, before
 * chaining to the blob's RX handler: it advances an internal counter, so it returns true on one
 * Final in CIRDIAG_CIR_EVERY and false on the rest. False whenever the dump is disarmed or the
 * chip-side CIA logging has not been enabled yet. Reading every block returns real CIR but costs
 * every range of the walk-up (bench run 5); this is the throttle that buys both. */
bool uwb_cirdiag_window_due(void);

/** @brief One-shot accumulator read diagnostic, for chasing a windowed-CIR dump that returns
 * non-physical taps. Reads the same 8-tap burst at three distinct accumulator offsets, then
 * repeats the first, then repeats it once more in DWT_CIR_READ_FULL so the raw 24-bit words are
 * visible before MID's sign-extend/shift/saturate. Emits plain `cir.probe:` lines (deliberately
 * NOT the [ALAB] prefix, so tools/aliro_lab.py ignores them).
 *
 * Reads as: passes 0..2 identical means the sample offset is being ignored; pass 0 differing from
 * pass 3 at the same offset means the read is racing something else on the bus; clk without the
 * ACC enable bits means dwt_readcir's own clock forcing did not stick.
 *
 * Console/task context only, and only worth running with the radio idle — it is several SPI
 * transactions with no regard for ranging deadlines. No-op until the chip-side CIA logging has
 * been enabled on the RX path (arm the stream and take one reception first). */
void uwb_cirdiag_probe(void);

#else

static inline void uwb_cirdiag_set_enabled(bool on)
{
	(void)on;
}
static inline bool uwb_cirdiag_enabled(void)
{
	return false;
}
static inline void uwb_cirdiag_dump_set_enabled(bool on)
{
	(void)on;
}
static inline bool uwb_cirdiag_dump_enabled(void)
{
	return false;
}
static inline uint32_t uwb_cirdiag_ring_count(void)
{
	return 0u;
}
static inline bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending)
{
	(void)status;
	(void)datalength;
	(void)deadline_pending;
	return false;
}
static inline void uwb_cirdiag_flush(void)
{
}
static inline bool uwb_cirdiag_window_due(void)
{
	return false;
}
static inline void uwb_cirdiag_probe(void)
{
}

#endif /* CONFIG_WOZ_UWB_CIRDIAG */

#ifdef __cplusplus
}
#endif

#endif /* WOZ_UWB_CIRDIAG_H_ */
