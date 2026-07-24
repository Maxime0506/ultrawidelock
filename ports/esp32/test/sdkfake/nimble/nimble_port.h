/* sdkfake nimble/nimble_port.h */
#ifndef SDKFAKE_NIMBLE_PORT_H
#define SDKFAKE_NIMBLE_PORT_H

#include "../esp_err.h"
#include "../host/ble_hs.h"

esp_err_t nimble_port_init(void);
void nimble_port_run(void); /* fake: returns immediately */
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);

extern esp_err_t fake_nimble_port_init_rc;
extern int fake_nimble_port_runs;

#endif
