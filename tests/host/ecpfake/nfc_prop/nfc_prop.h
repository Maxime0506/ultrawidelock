/* ecpfake: minimal nfc_prop/nfc_prop.h — the module's own public surface. */
#ifndef ECPFAKE_NFC_PROP_H
#define ECPFAKE_NFC_PROP_H

#include "rfal_nfc.h"

void NfcPropInit(void);
const rfalNfcPropCallbacks *NfcPropGetCallbacks(void);

#endif /* ECPFAKE_NFC_PROP_H */
