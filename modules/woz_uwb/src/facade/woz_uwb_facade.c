// UWB facade: binds the CCC credential-based STS engine to the DW3000 radio, exposes Aliro DS-TWR
// responder start/stop and range query, and manages platform dependencies (HFCLK boost, SPI init,
// callbacks).
/*
 * C shim over fira_session + ccc_shim (see woz_uwb_facade.h).
 */
#include "woz_uwb_facade.h"
#include <errno.h>
#include "aliro_kdf.h"
#include "fira_session.h"
#include "ccc_shim.h"
#include "flight_recorder.h" /* fr_capture_config — walk-up record/replay (gated) */

/**
 * @brief One-shot boost of the app-core HFCLK to 128 MHz for the DW3000 SPI bus.
 *
 * nRF5340-specific platform seam: the app core boots with HFCLK divided to
 * 64 MHz. Other SoCs (e.g. ESP32-S3) clock their SPI controller independently,
 * so this compiles to a no-op there.
 */
static void woz_hfclk_ensure_128mhz(void)
{
	/* No-op by design: the boost happens in woz_hfclk_boost()
	 * (ports/zephyr/uwb/woz_hfclk_boost.c) at PRE_KERNEL_1, before the SPI
	 * driver is ever configured. Kept as a seam so the call sites and the
	 * non-nRF5340 ports build unchanged. */
}

/**
 * @brief Bind the CCC STS from the add-on-supplied plaintext URSK; returns 0 on success.
 * @param ursk Pointer to the URSK bytes.
 * @param ursk_len Length of the URSK.
 * @return 0 on success, -EINVAL if ursk is NULL.
 */
int woz_uwb_bind_ursk(const uint8_t *ursk, size_t ursk_len)
{
	woz_hfclk_ensure_128mhz();
	fira_session_set_provisioned_ursk(ursk);
	/* Placeholder ranging_config / sts_index0 / n_slot until Aliro M1-M4 negotiation. */
	return ccc_shim_bind_from_ursk(ursk, ursk, ursk_len, 0u, 8u);
}

/**
 * @brief Start the CCC DS-TWR responder bound to a live Aliro credential; returns 0 on success.
 * @param c Configuration struct (channel, sync_code_index, ursk, ranging_config, sts_index0,
 * slot_per_round).
 * @return 0 on success, -EINVAL if config is NULL or ursk is NULL, -EIO if radio initialization
 * fails.
 */
int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *c)
{
	if (c == NULL || c->ursk == NULL) {
		return -EINVAL;
	}

	woz_hfclk_ensure_128mhz();
	/* Trust is session-bound. Carrying the prior session's K agreeing blocks
	 * into a new URSK would let its first measurement appear trusted. */
#if defined(CONFIG_WOZ_ALIRO)
	fira_session_reset_ranges();
#endif

	/* Flight recorder: stamp the session config (incl. URSK) that opens this
	 * walk-up, so a replay can reconstruct the exact session before feeding it
	 * the recorded frames. No-op unless armed (`fr on`). */
	fr_capture_config(c);

	/* Stash the URSK so the Pre-POLL decode can derive the CCC STS the Wallet expects. */
	fira_session_set_provisioned_ursk(c->ursk);
	/* Bind the shim's SaltedHash to the RangingConfiguration when supplied; else fall back to
	 * the URSK. */
	if (c->ranging_config != NULL && c->rc_len > 0u) {
		ccc_shim_bind_from_ursk(c->ursk, c->ranging_config, c->rc_len, c->sts_index0,
					c->slot_per_round ? c->slot_per_round : 1u);
	} else {
		ccc_shim_bind_from_ursk(c->ursk, c->ursk, ALIRO_URSK_LEN, c->sts_index0,
					c->slot_per_round ? c->slot_per_round : 1u);
	}
	/* Fresh per-session log budget so a live Wallet session re-logs its own RX-arms. */
	ccc_shim_rx_log_reset();

	/* Stand up the permanent SP0 receiver on modules/woz_dw3000, driving the DS-TWR exchange. */
	return ccc_prepoll_listen(c->channel, c->sync_code_index);
}

/**
 * @brief Pre-apply the expected session PHY so the M4-time start skips dwt_configure.
 * @param channel UWB channel the upcoming session is expected to negotiate.
 * @param sync_code_index Expected SYNC code index.
 * @return 0 on success; the M4 start recovers with a full configure on any failure.
 */
int woz_uwb_prewarm(uint8_t channel, uint8_t sync_code_index)
{
	woz_hfclk_ensure_128mhz();
	return ccc_prepoll_prewarm(channel, sync_code_index);
}

/**
 * @brief Quiesce the radio and unbind the CCC STS shim.
 */
void woz_uwb_stop(void)
{
	/* Unbind the CCC STS so the permanent Pre-POLL receiver ignores frames. */
	ccc_shim_unbind();
	/* Close the listen-gate and force the DW3000 out of RX so the SP0
	 * Pre-POLL listener stops self-rearming (RX LED dark until restart). */
	ccc_prepoll_stop();
}

/**
 * @brief Retrieve the last valid DS-TWR distance measurement in centimeters.
 * @param cm_out Pointer to store the distance in cm.
 * @return True if a valid range has been seen since initialization; false otherwise.
 */
bool woz_uwb_last_range_cm(int32_t *cm_out)
{
	return fira_session_last_range(cm_out, NULL, NULL, NULL, NULL);
}

/**
 * @brief Retrieve the last valid DS-TWR distance in centimeters, gated by range-integrity
 * consensus.
 * @param cm_out Pointer to store the distance in cm.
 * @return True only when a valid range has been seen AND it is trusted by the layer-4 consensus
 * gate; false if no valid range exists or the range is not yet trusted. When CONFIG_WOZ_ALIRO is
 * not defined, behaves identically to woz_uwb_last_range_cm().
 */
/**
 * @brief Register a callback fired after each accepted DS-TWR range latch.
 * @param cb Callback invoked on the UWB RX path (keep it to a task wake), or NULL to clear.
 * Without CONFIG_WOZ_ALIRO there is no range latch to observe and this is a no-op.
 */
void woz_uwb_set_range_listener(void (*cb)(void))
{
#if defined(CONFIG_WOZ_ALIRO)
	fira_session_set_range_listener(cb);
#else
	(void)cb;
#endif
}

bool woz_uwb_trusted_range_cm(int32_t *cm_out)
{
	return woz_uwb_trusted_range_age_cm(cm_out, NULL);
}

bool woz_uwb_trusted_range_age_cm(int32_t *cm_out, int64_t *age_ms_out)
{
#if defined(CONFIG_WOZ_ALIRO)
	/* Layer 4: only surface the range to the unlock seam once K consecutive
	 * agreeing plausible blocks have built trust, so a lone spoofed block
	 * cannot flip open-allowed. */
	return fira_session_last_range(cm_out, NULL, NULL, NULL, age_ms_out) &&
	       fira_session_range_trusted();
#else
	return fira_session_last_range(cm_out, NULL, NULL, NULL, age_ms_out);
#endif
}

uint32_t woz_uwb_range_generation(void)
{
#if defined(CONFIG_WOZ_ALIRO)
	return fira_session_range_generation();
#else
	return 0u;
#endif
}

bool woz_uwb_trusted_range_after_cm(int32_t *cm_out, uint32_t after)
{
#if defined(CONFIG_WOZ_ALIRO)
	/* Generation is written after the range fields. A concurrent latch can
	 * cause one harmless false-negative poll, never acceptance of old data. */
	uint32_t generation = fira_session_range_generation();

	return (int32_t)(generation - after) > 0 && woz_uwb_trusted_range_cm(cm_out);
#else
	(void)after;
	return woz_uwb_trusted_range_cm(cm_out);
#endif
}

bool woz_uwb_trusted_range_after_checked_cm(int32_t *cm_out, uint32_t after,
					    struct woz_uwb_range_integrity *ig_out)
{
	/* Default to a failed STS before anything else runs, so every early return
	 * below -- and every build without a ranging layer -- leaves the caller
	 * holding "not vouched for" rather than an uninitialised verdict. */
	if (ig_out != NULL) {
		ig_out->sts_ok = false;
		ig_out->sts_quality = 0;
		ig_out->trust_level = 0u;
	}
	if (!woz_uwb_trusted_range_after_cm(cm_out, after)) {
		return false;
	}
#if defined(CONFIG_WOZ_ALIRO)
	struct fira_range_integrity ig;

	if (ig_out != NULL && fira_session_last_range_integrity(&ig)) {
		ig_out->sts_ok = ig.sts_ok;
		ig_out->sts_quality = ig.sts_quality;
		ig_out->trust_level = ig.trust_level;
	}
#endif
	return true;
}
