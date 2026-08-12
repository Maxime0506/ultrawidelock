#ifndef TEST_FAKE_NIMBLE_HOST_H
#define TEST_FAKE_NIMBLE_HOST_H

extern unsigned fake_nimble_port_init_calls;
extern unsigned fake_nimble_port_run_calls;
extern unsigned fake_ble_hs_sched_start_calls;
/*
 * Live task count sampled inside each call. The MPSL worker already exists by
 * then, so ordering is checked by the difference between these, not by zero.
 */
extern unsigned fake_nimble_port_init_task_count;
extern unsigned fake_ble_hs_sched_start_task_count;

void fake_nimble_host_reset(void);

#endif /* TEST_FAKE_NIMBLE_HOST_H */
