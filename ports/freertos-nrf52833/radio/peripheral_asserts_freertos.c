/*
 * Peripheral collision assertions, in the one place that can see every claim.
 *
 * peripherals.yml is the record of who owns what on this part, but a record is
 * not a check. Three stacks share one PPI and GPIOTE space here -- MPSL, the
 * nRF 802.15.4 driver, and this port's DW3110 wiring -- and a double claim is
 * silent: the peripheral works until the other owner happens to be active,
 * which on a lock means it works on the bench and fails in a hallway.
 *
 * This file exists because no other one can do the job. The 802.15.4 driver's
 * masks are in its private source tree, which only the woz_802154 layer has on
 * its include path; the DW3110's channel is in the UWB layer's board_pins.h.
 * A file compiled into woz_802154 that includes board_pins.h sees both, and
 * nothing else in the image does.
 *
 * It has no code on purpose. The whole content is assertions, and it is in the
 * library's source list rather than a header so that it is compiled exactly
 * once and cannot be dropped by whoever forgets to include it.
 */
#include "board_pins.h"

#include "nrf_802154_debug_peripherals.h"
#include "nrf_802154_peripherals.h"

/*
 * The DW3110 interrupt line against the 802.15.4 driver's debug pins.
 *
 * This is live in every build, not only debug ones. The mask is defined either
 * way -- zero without ENABLE_DEBUG_GPIO, channels 0 through 5 with it -- so
 * guarding the assertion on that macro would have made it unfireable in exactly
 * the configuration that is shipped, which is not a check at all. Left
 * unguarded, defining ENABLE_DEBUG_GPIO fails the build instead of producing a
 * DW3110 interrupt that quietly stops arriving on a build made specifically to
 * observe radio timing.
 */
_Static_assert((NRF_802154_DEBUG_GPIOTE_CHANNELS_USED_MASK &
		(1u << ULTRAWIDELOCK_DW3000_GPIOTE_CHANNEL)) == 0u,
	       "the DW3110 IRQ shares a GPIOTE channel with the 802.15.4 driver's debug pins; "
	       "move ULTRAWIDELOCK_DW3000_GPIOTE_CHANNEL above 5, or drop ENABLE_DEBUG_GPIO");

/*
 * And against the driver's PPI claim, which is not debug-only and is the larger
 * risk of the two: these channels are taken whenever the radio is enabled.
 */
_Static_assert((NRF_802154_PPI_CHANNELS_USED_MASK &
		NRF_802154_DEBUG_PPI_CHANNELS_USED_MASK) ==
		       NRF_802154_DEBUG_PPI_CHANNELS_USED_MASK ||
		       NRF_802154_DEBUG_PPI_CHANNELS_USED_MASK == 0u,
	       "the 802.15.4 driver's debug PPI channels are outside the set it reserves, so "
	       "enabling them would collide with MPSL rather than with the driver");

/*
 * MPSL's side of the GPIOTE question is deliberately not asserted here, and the
 * omission is a finding rather than a gap.
 *
 * MPSL claims GPIOTE only for a front-end module, and a FEM's channel is
 * supplied at runtime through mpsl_fem_config_common.h rather than fixed by a
 * macro a header could read. This image links libmpsl_fem_common.a -- the
 * dispatch layer -- with no device FEM archive behind it, which is to say no
 * FEM, which is to say MPSL claims no GPIOTE channel. That is a property of the
 * link line, checked by which archives are named in CMakeLists.txt, and it
 * stops being true the moment someone adds a FEM. Whoever does that owns
 * re-answering this, and there is no compile-time check that will remind them.
 */
