/* WozNfc backend for boards with no NFC frontend: polling never starts and no
 * NFC session is ever created, so Send()/Terminate() are unreachable in a
 * correct run; Send() reports invalid state defensively. */

#include <woz_nfc/transport.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(woz_nfc_none, CONFIG_WOZ_NFC_LOG_LEVEL);

namespace WozNfc
{

AliroError Init()
{
	return ALIRO_NO_ERROR;
}

AliroError Start()
{
	LOG_INF("No NFC reader fitted; Aliro NFC flow disabled");
	return ALIRO_NO_ERROR;
}

AliroError Stop()
{
	return ALIRO_NO_ERROR;
}

AliroError Send(Aliro::Data)
{
	return ALIRO_INVALID_STATE;
}

AliroError Terminate()
{
	return ALIRO_NO_ERROR;
}

} // namespace WozNfc
