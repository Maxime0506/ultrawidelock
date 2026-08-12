/*
 * UltraWideLock NFC transport seam.
 *
 * One reader backend is selected at build time (Kconfig choice ULTRAWIDELOCK_NFC_TRANSPORT):
 * the upstream ST25R/RFAL transport, the in-tree PN532 transport, or none. The
 * add-on application calls these five functions instead of a concrete transport
 * class; the selected backend supplies the definitions. The semantics mirror the
 * upstream NfcTransportRfal public API exactly:
 *
 *  - Init():  bring up the bus/PAL. Failure is logged by the caller but not fatal.
 *  - Start(): begin polling for a User Device. May be called again after Stop().
 *  - Stop():  cease polling and switch the RF field off.
 *  - Send():  asynchronous. Queues one APDU for the activated device and returns;
 *             the response is delivered later via AliroStack::HandleSessionData()
 *             from the credential workqueue. Returns ALIRO_INVALID_STATE when no
 *             device is activated.
 *  - Terminate(): the stack is done with the session; drop the device and return
 *             to polling. Does not call back into the stack.
 *
 * The backend owns the session lifecycle in the other direction: on ISO-DEP
 * activation it calls AliroStack::CreateSession(ConnectionHandle::Nfc()), on
 * device loss or exchange failure DestroySession(), both from the credential
 * workqueue, matching the upstream RFAL transport's threading.
 */
#pragma once

#include <aliro/errors.h>
#include <aliro/types.h>

/**
 * C++ namespace for NFC transport: Init, Start, Stop, Send (Aliro::Data), Terminate.
 */
namespace UltraWideLockNfc
{

AliroError Init();
AliroError Start();
AliroError Stop();
AliroError Send(Aliro::Data data);
AliroError Terminate();

} // namespace UltraWideLockNfc
