/** @file uwb_seam.h — the DW3000 entry points the CCC engine owns.
 *
 * Four decadriver calls carry engine behaviour that no caller may skip: arming
 * RX must program the CCC STS for the slot, loading an STS-IV must substitute
 * the CCC STS-V, registering callbacks must insert the Pre-POLL shim, and
 * (re)configuring the PHY is traced. Every call site in this module goes
 * through the helpers below instead of <deca_device_api.h>, so a site added
 * later cannot quietly bypass any of it. `make seam` enforces that
 * mechanically (tests/tooling/uwb_seam_check.sh).
 *
 * Under CONFIG_WOZ_ALIRO the engine supplies the definitions:
 *
 *   ultrawidelock_uwb_arm_rx         ccc_shim_rx.c    program the CCC key/IV, then arm RX
 *   ultrawidelock_uwb_set_sts_iv     ccc_shim_wrap.c  substitute the CCC STS-V per frame
 *   ultrawidelock_uwb_set_callbacks  uwb_rxdiag.c     insert the tally + Pre-POLL shims
 *   ultrawidelock_uwb_configure_phy  uwb_rxdiag.c     log the PHY configuration
 *
 * The ESP32 port omits uwb_rxdiag.c (it is k_work-based) and supplies the last
 * two from port/ultrawidelock_seam_stubs.c instead. Below the CONFIG_WOZ_ALIRO tier there
 * is no engine to reach, so each helper inlines to the plain decadriver call.
 *
 * The implementations are free to call the decadriver directly — that is how
 * they reach the hardware, and how a site that has already programmed the STS
 * itself (the self-rearm paths in ccc_shim_rx.c) opts out on purpose.
 */

#ifndef UWB_SEAM_H
#define UWB_SEAM_H

#include <stdint.h>

#include <deca_device_api.h>

#ifdef CONFIG_WOZ_ALIRO

/** @brief Program the CCC STS for the current slot, then enable RX. */
int32_t ultrawidelock_uwb_arm_rx(int32_t mode);

/** @brief Load an STS-IV, substituting the CCC STS-V while the shim is bound. */
void ultrawidelock_uwb_set_sts_iv(dwt_sts_cp_iv_t *iv);

/** @brief Register RX/TX callbacks, inserting the engine's shims ahead of them. */
void ultrawidelock_uwb_set_callbacks(dwt_callbacks_s *callbacks);

/** @brief Apply a full PHY configuration. */
int32_t ultrawidelock_uwb_configure_phy(dwt_config_t *config);

#else /* no engine below this tier — go straight to the radio */

/** @brief Enable RX. No CCC STS exists at this tier, so this is the bare call. */
static inline int32_t ultrawidelock_uwb_arm_rx(int32_t mode)
{
	return dwt_rxenable(mode);
}

/** @brief Load an STS-IV verbatim: there is no CCC STS-V to substitute here. */
static inline void ultrawidelock_uwb_set_sts_iv(dwt_sts_cp_iv_t *iv)
{
	dwt_configurestsiv(iv);
}

/** @brief Register callbacks with no shim in front of them. */
static inline void ultrawidelock_uwb_set_callbacks(dwt_callbacks_s *callbacks)
{
	dwt_setcallbacks(callbacks);
}

/** @brief Apply a full PHY configuration, untraced. */
static inline int32_t ultrawidelock_uwb_configure_phy(dwt_config_t *config)
{
	return dwt_configure(config);
}

#endif /* CONFIG_WOZ_ALIRO */

#endif /* UWB_SEAM_H */
