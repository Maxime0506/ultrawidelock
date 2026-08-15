/* SPDX-License-Identifier: ISC */

/* nRF5340 app-core HFCLK boost, split out of ultrawidelock_uwb_facade.c so the facade
 * carries no Zephyr include. The facade's ultrawidelock_hfclk_ensure_128mhz() stays a
 * portable no-op seam; this file is the platform half behind it. */
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
#include <hal/nrf_clock.h>
#include <zephyr/init.h>

/**
 * @brief Raise the app-core HFCLK to 128 MHz before any driver initialises.
 *
 * The app core boots with HFCLK divided to 64 MHz. What matters here is the
 * timing of the boost, not the boost itself: this used to run lazily on the
 * first ranging call, changing the core clock domain underneath an already
 * configured and actively used SPIM4. Doing it at PRE_KERNEL_1 means every
 * driver is configured once, against a clock that then never moves.
 *
 * Note what this is NOT for. spi_nrfx_spim only consults the divider to cap
 * max_freq, and only when the requested rate exceeds 16 MHz (spi_nrfx_spim.c:182).
 * The DW3000 is at 8 MHz, so that clause never fires and the divider has no
 * bearing on the SPI bus rate either way.
 */
static int ultrawidelock_hfclk_boost(void)
{
	nrf_clock_hfclk_div_set(NRF_CLOCK, NRF_CLOCK_HFCLK_DIV_1);
	return 0;
}
SYS_INIT(ultrawidelock_hfclk_boost, PRE_KERNEL_1, 0);
#endif
