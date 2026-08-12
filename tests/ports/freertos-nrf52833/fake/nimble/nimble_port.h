/*
 * Recording double for the NimBLE porting-layer entry points. Prototypes
 * mirror the pinned NimBLE header and are asserted against it by
 * scripts/freertos-ble-source-check.sh.
 */
#ifndef TEST_NIMBLE_NIMBLE_PORT_H
#define TEST_NIMBLE_NIMBLE_PORT_H

void nimble_port_init(void);
void nimble_port_run(void);

#endif /* TEST_NIMBLE_NIMBLE_PORT_H */
