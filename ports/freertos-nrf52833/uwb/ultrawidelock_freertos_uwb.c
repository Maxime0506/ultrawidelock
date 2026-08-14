/*
 * The port's bring-up of the DW3110. ultrawidelock_freertos_uwb.h says why it exists;
 * this says why it is four lines long.
 *
 * This is deliberately thin, and the thinness is the design. Bring-up on this
 * part is the engine's job: uwb_min_radio_init() owns the order -- SPI up, the
 * reset sequence, the device-ID read, dwt_initialise, then the interrupt line
 * -- and it owns it identically for all three ports. A second ordering written
 * here would be a fourth opinion about a sequence that already has one, and it
 * would drift from the other two the first time the engine changed.
 *
 * So the port reaches the engine through the same contract a credential session
 * uses and adds nothing to it. What is genuinely this file's is the decision of
 * when to call, what to do when it fails, and the PHY to ask for.
 */
#include <stdbool.h>

#include <ccc_shim.h>
#include <ultrawidelock/uwb.h>

#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_uwb.h"

#define UWB_TAG "uwb"

/*
 * Channel 9, SYNC code 9. Not a guess and not a default: this is the single UWB
 * configuration the reader offers in M1, so it is the one every session
 * negotiates in practice. modules/ultrawidelock_cred/src/ultrawidelock_ranging.c pre-applies the
 * same pair for the same reason, and the two must not drift -- a bring-up that
 * warmed a different PHY would leave the shim's cache cold at M4 and put
 * dwt_configure back on the critical path it was moved off.
 */
#define UWB_PREWARM_CHANNEL 9u
#define UWB_PREWARM_SYNC_CODE 9u

static bool s_ready;

int ultrawidelock_freertos_uwb_start(void)
{
	int rc = ultrawidelock_uwb_prewarm(UWB_PREWARM_CHANNEL, UWB_PREWARM_SYNC_CODE);

	if (rc != 0) {
		/*
		 * Everything that can fail here fails on the wire: no answer to
		 * the device-ID read, a part that will not leave IDLE_RC, a
		 * reset line that is not connected. The engine's code is the
		 * one useful thing to carry, because those cases are told apart
		 * by which step returned, not by anything visible from here.
		 */
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, UWB_TAG,
					   "DW3110 bring-up failed (%d)", rc);
		s_ready = false;
		return rc;
	}

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, UWB_TAG,
				   "DW3110 up, PHY ch%u code%u pre-applied",
				   (unsigned)UWB_PREWARM_CHANNEL, (unsigned)UWB_PREWARM_SYNC_CODE);
	s_ready = true;
	return 0;
}

bool ultrawidelock_freertos_uwb_ready(void)
{
	return s_ready;
}

/* Overrides the weak default in board/flash_freertos.c; the header says why. */
bool ultrawidelock_freertos_uwb_ranging_active(void)
{
	return ccc_prepoll_listening();
}
