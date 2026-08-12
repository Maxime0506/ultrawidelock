/* UltraWideLockNfc backend for boards with no NFC frontend: polling never starts and no
 * NFC session is ever created, so Send()/Terminate() are unreachable in a
 * correct run; Send() reports invalid state defensively. */

#include <ultrawidelock_nfc/transport.h>

#include "woz_log.h"

LOG_MODULE_REGISTER(ultrawidelock_nfc_none, CONFIG_ULTRAWIDELOCK_NFC_LOG_LEVEL);

namespace UltraWideLockNfc
{

/**
 * Initialize the no-op NFC transport; always succeeds.
 */
AliroError Init()
{
	return ALIRO_NO_ERROR;
}

/**
 * Start the no-op NFC transport; logs that NFC is disabled and returns success. Called during
 * system initialization when no NFC reader is fitted.
 */
AliroError Start()
{
	LOG_INF("No NFC reader fitted; Aliro NFC flow disabled");
	return ALIRO_NO_ERROR;
}

/**
 * Stop the no-op NFC transport; always succeeds.
 */
AliroError Stop()
{
	return ALIRO_NO_ERROR;
}

/**
 * Attempt to send data on the no-op NFC transport; always fails with ALIRO_INVALID_STATE because no
 * transport is active.
 */
AliroError Send(Aliro::Data)
{
	return ALIRO_INVALID_STATE;
}

/**
 * Terminate the no-op NFC transport; always succeeds.
 */
AliroError Terminate()
{
	return ALIRO_NO_ERROR;
}

} // namespace UltraWideLockNfc
