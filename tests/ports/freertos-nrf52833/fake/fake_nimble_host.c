#include "fake_nimble_host.h"

#include "fake_freertos.h"

#include <host/ble_hs.h>
#include <nimble/nimble_port.h>

struct ble_hs_cfg ble_hs_cfg;

unsigned fake_nimble_port_init_calls;
unsigned fake_nimble_port_run_calls;
unsigned fake_ble_hs_sched_start_calls;
unsigned fake_nimble_port_init_task_count;
unsigned fake_ble_hs_sched_start_task_count;

void fake_nimble_host_reset(void)
{
	fake_nimble_port_init_calls = 0;
	fake_nimble_port_run_calls = 0;
	fake_ble_hs_sched_start_calls = 0;
	fake_nimble_port_init_task_count = 0;
	fake_ble_hs_sched_start_task_count = 0;
	ble_hs_cfg.sync_cb = NULL;
	ble_hs_cfg.reset_cb = NULL;
}

void nimble_port_init(void)
{
	fake_nimble_port_init_calls++;
	fake_nimble_port_init_task_count = fake_task_count;
}

void nimble_port_run(void)
{
	fake_nimble_port_run_calls++;
	/* Returns instead of looping forever so the test can pump the task. */
}

void ble_hs_sched_start(void)
{
	fake_ble_hs_sched_start_calls++;
	fake_ble_hs_sched_start_task_count = fake_task_count;
}
