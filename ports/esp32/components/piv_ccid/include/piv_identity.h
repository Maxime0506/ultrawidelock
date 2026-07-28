#ifndef WOZ_PIV_IDENTITY_H
#define WOZ_PIV_IDENTITY_H

#include "piv_apdu.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load or create the persistent PIV authentication identity. A new identity is
 * committed as one NVS blob before it is exposed to the USB stack.
 */
esp_err_t piv_identity_init(void);

/* Backend for slot 9A certificate, PIN policy, and presence-gated signing. */
const struct piv_apdu_backend *piv_identity_backend(void);

#ifdef __cplusplus
}
#endif

#endif
