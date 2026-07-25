/* WozNfc backend forwarding to the add-on's ST25R/RFAL transport unchanged. */

#include <woz_nfc/transport.h>

#include "aliro/platform/nfc/nfc_transport_rfal.h"

namespace WozNfc
{

AliroError Init()
{
	return Aliro::NfcTransportRfal::Instance().Init();
}

AliroError Start()
{
	return Aliro::NfcTransportRfal::Instance().Start();
}

AliroError Stop()
{
	return Aliro::NfcTransportRfal::Instance().Stop();
}

AliroError Send(Aliro::Data data)
{
	return Aliro::NfcTransportRfal::Instance().Send(data);
}

AliroError Terminate()
{
	return Aliro::NfcTransportRfal::Instance().Terminate();
}

} // namespace WozNfc
