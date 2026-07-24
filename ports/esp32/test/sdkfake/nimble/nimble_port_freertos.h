/* sdkfake nimble/nimble_port_freertos.h */
#ifndef SDKFAKE_NIMBLE_PORT_FREERTOS_H
#define SDKFAKE_NIMBLE_PORT_FREERTOS_H

void nimble_port_freertos_init(void (*host_task_fn)(void *param));
void nimble_port_freertos_deinit(void);

extern void (*fake_nimble_host_task)(void *);
extern int fake_nimble_freertos_deinits;

#endif
