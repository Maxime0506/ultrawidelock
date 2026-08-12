/* UltraWideLockNfc backend forwarding to the add-on's ST25R/RFAL transport unchanged. */

#include <ultrawidelock_nfc/transport.h>

#include "aliro/platform/nfc/nfc_transport_rfal.h"

namespace UltraWideLockNfc
{

/**
 * Initialize the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Init().
 */
AliroError Init()
{
	return Aliro::NfcTransportRfal::Instance().Init();
}

/**
 * Start the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Start().
 */
AliroError Start()
{
	return Aliro::NfcTransportRfal::Instance().Start();
}

/**
 * Stop the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Stop().
 */
AliroError Stop()
{
	return Aliro::NfcTransportRfal::Instance().Stop();
}

/**
 * Send data on the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Send(data).
 */
AliroError Send(Aliro::Data data)
{
	return Aliro::NfcTransportRfal::Instance().Send(data);
}

/**
 * Terminate the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Terminate().
 */
AliroError Terminate()
{
	return Aliro::NfcTransportRfal::Instance().Terminate();
}

} // namespace UltraWideLockNfc
