/* ecpfake: minimal rfal_nfc.h — just the proprietary-callback table
 * nfc_prop_ecp.cpp populates (member names/order match the designated
 * initializer in that file). */
#ifndef ECPFAKE_RFAL_NFC_H
#define ECPFAKE_RFAL_NFC_H

#include "rfal_rf.h"

typedef struct {
	ReturnCode (*rfalNfcpPollerInitialize)(void);
	ReturnCode (*rfalNfcpPollerTechnologyDetection)(void);
	ReturnCode (*rfalNfcpPollerStartCollisionResolution)(void);
	ReturnCode (*rfalNfcpPollerGetCollisionResolutionStatus)(void);
	ReturnCode (*rfalNfcpStartActivation)(void);
	ReturnCode (*rfalNfcpGetActivationStatus)(void);
} rfalNfcPropCallbacks;

#endif /* ECPFAKE_RFAL_NFC_H */
