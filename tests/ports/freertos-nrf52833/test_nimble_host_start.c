/*
 * The NimBLE host sequencer sits on top of the radio sequencer, and both own
 * one-shot static state, so each scenario runs in its own forked process.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fake_freertos.h"
#include "fake_nimble.h"
#include "fake_nimble_host.h"
#include "fake_nrf.h"
#include "fake_sdc.h"
#include "ultrawidelock_freertos_nimble_host.h"
#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_radio.h"

#include <host/ble_hs.h>
#include <nrf_errno.h>
#include <sdc_hci.h>

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                    \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                              \
			printf("  FAIL %s\n", (label));                                            \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                            \
		}                                                                                  \
	} while (0)

/* BSP hooks the port expects from the board layer. */
static unsigned g_fatal_calls;

int64_t ultrawidelock_freertos_uptime_us(void)
{
	return fake_uptime_us;
}

void ultrawidelock_freertos_busy_wait_us(uint64_t us)
{
	fake_uptime_us += (int64_t)us;
}

uint32_t ultrawidelock_freertos_cycle_get_32(void)
{
	return (uint32_t)(fake_uptime_us * 64);
}

int ultrawidelock_freertos_entropy(void *buffer, size_t length)
{
	memset(buffer, 0x5a, length);
	return 0;
}

int8_t ultrawidelock_freertos_die_temperature_c(void)
{
	return 23;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	g_fatal_calls++;
	printf("  FAIL unexpected fatal: %s\n", reason != NULL ? reason : "?");
	exit(1);
}

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

void ultrawidelock_freertos_log_hexdump(enum ultrawidelock_freertos_log_level level,
					const char *tag, const void *data, size_t len,
					const char *message)
{
	(void)level;
	(void)tag;
	(void)data;
	(void)len;
	(void)message;
}

/*
 * The real dispatcher is vendor source that only the opt-in
 * freertos-hci-dispatcher-check builds, so the adapter in
 * ble/hci_dispatcher_freertos.c is linked here against these two stubs. That
 * keeps the wiring from ultrawidelock_freertos_nimble_host_start() down to the vendor
 * entry points under test without pulling the vendor tree into this binary.
 */
int hci_internal_cmd_put(uint8_t *cmd_in)
{
	(void)cmd_in;
	return 0;
}

int hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out)
{
	(void)msg_out;
	(void)msg_type_out;
	return -NRF_EAGAIN;
}

static void scenario_success(void)
{
	void (*host_entry)(void *);
	void *host_arg;
	unsigned tasks_before;

	CHECK("the host is not ready before it is started",
	      !ultrawidelock_freertos_nimble_host_ready() &&
		      !ultrawidelock_freertos_nimble_host_synced() &&
		      ultrawidelock_freertos_nimble_host_resets() == 0);

	tasks_before = fake_task_count;
	CHECK("host start brings up the radio and the host",
	      ultrawidelock_freertos_nimble_host_start() == 0 &&
		      ultrawidelock_freertos_nimble_host_ready());
	host_entry = fake_task_entry;
	host_arg = fake_task_arg;

	CHECK("the controller is enabled before the host is initialized",
	      fake_mpsl_init_calls == 1 && fake_sdc_enable_calls == 1 &&
		      ultrawidelock_freertos_radio_ready());
	CHECK("the porting layer is initialized exactly once",
	      fake_nimble_port_init_calls == 1);

	/*
	 * nimble_port_init() is what initializes the transport, so the host task
	 * must not exist yet when it runs and must exist by the time startup is
	 * scheduled.
	 */
	CHECK("the host task is created between port init and scheduled start",
	      fake_ble_hs_sched_start_task_count == fake_nimble_port_init_task_count + 1 &&
		      fake_task_count == fake_ble_hs_sched_start_task_count);
	/*
	 * The MPSL worker is the other one: the radio brings it up first. On the
	 * target a third appears, because the real nimble_port_init() runs
	 * ble_transport_ll_init() and that creates the HCI receive task; the
	 * recording double here does not.
	 */
	CHECK("starting the host adds only the worker and the host task",
	      fake_task_count == tasks_before + 2);

	/*
	 * 6144, matching the port. The size is load-bearing and worth pinning:
	 * the CoC receive path runs the credential access protocol's P-256 crypto on
	 * this stack, and 4096 overflowed on hardware on 2026-08-14 during the
	 * first full-auth walk-up -- the same fault the oracle hit twice before
	 * raising its equivalent thread to 6144 with a painted peak of 3,872 B.
	 * A future shrink should have to argue with this line.
	 */
	CHECK("the host task runs below the controller receive pump",
	      fake_task_priority == (UBaseType_t)(tskIDLE_PRIORITY + 1) &&
		      fake_task_stack_depth == 6144u / sizeof(StackType_t));

	CHECK("startup is scheduled onto the host event queue, not run inline",
	      fake_ble_hs_sched_start_calls == 1 && fake_nimble_port_run_calls == 0);

	CHECK("the host registers both of its lifecycle callbacks",
	      ble_hs_cfg.sync_cb != NULL && ble_hs_cfg.reset_cb != NULL);
	CHECK("a started host is not yet synced with the controller",
	      !ultrawidelock_freertos_nimble_host_synced());

	host_entry(host_arg);
	CHECK("the host task drains the default event queue",
	      fake_nimble_port_run_calls == 1);

	ble_hs_cfg.sync_cb();
	CHECK("the sync callback marks the host usable", ultrawidelock_freertos_nimble_host_synced());

	ble_hs_cfg.reset_cb(NRF_EPERM);
	CHECK("a host reset withdraws sync and is counted, without faulting",
	      !ultrawidelock_freertos_nimble_host_synced() &&
		      ultrawidelock_freertos_nimble_host_resets() == 1 && g_fatal_calls == 0);

	ble_hs_cfg.sync_cb();
	CHECK("the host resynchronizes after a reset",
	      ultrawidelock_freertos_nimble_host_synced() &&
		      ultrawidelock_freertos_nimble_host_resets() == 1);

	CHECK("host start is idempotent",
	      ultrawidelock_freertos_nimble_host_start() == 0 && fake_nimble_port_init_calls == 1 &&
		      fake_ble_hs_sched_start_calls == 1 && fake_task_count == tasks_before + 2);
}

static void scenario_radio_failure(void)
{
	fake_sdc_enable_result = -NRF_ENOMEM;

	CHECK("a radio that will not start stops the host",
	      ultrawidelock_freertos_nimble_host_start() ==
			      -ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_STAGE_RADIO &&
		      !ultrawidelock_freertos_nimble_host_ready());
	CHECK("a failed radio never initializes the porting layer",
	      fake_nimble_port_init_calls == 0 && fake_ble_hs_sched_start_calls == 0);
}

static void scenario_task_failure(void)
{
	/* The MPSL worker is created first, so let that one through. */
	fake_task_create_failures = 0;

	CHECK("the radio starts before the host task is attempted",
	      ultrawidelock_freertos_radio_start(ultrawidelock_freertos_radio_sdc_dispatcher()) == 0);

	fake_task_create_failures = 1;
	CHECK("a host task that cannot be created stops the host",
	      ultrawidelock_freertos_nimble_host_start() ==
			      -ULTRAWIDELOCK_FREERTOS_NIMBLE_HOST_STAGE_TASK &&
		      !ultrawidelock_freertos_nimble_host_ready());
	CHECK("a host without its task never schedules startup",
	      fake_ble_hs_sched_start_calls == 0 && fake_nimble_port_run_calls == 0);
}

static int run_scenario(int scenario)
{
	fake_freertos_reset();
	fake_nimble_reset();
	fake_nimble_host_reset();
	fake_nrf_reset();
	fake_sdc_reset();

	switch (scenario) {
	case 0:
		scenario_success();
		break;
	case 1:
		scenario_radio_failure();
		break;
	default:
		scenario_task_failure();
		break;
	}
	return g_failures == 0 ? 0 : 1;
}

static bool run_scenario_child(int scenario, const char *label)
{
	pid_t pid;
	int status = 0;

	/* Flush first so the forked buffer copy cannot replay the parent's output. */
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int rc = run_scenario(scenario);

		fflush(stdout);
		_exit(rc);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		printf("  FAIL %s (could not fork scenario)\n", label);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	static const struct {
		int scenario;
		const char *label;
	} scenarios[] = {
		{0, "the host starts, syncs, and survives a controller reset"},
		{1, "a failed radio startup stops the host"},
		{2, "a failed host task creation stops the host"},
	};
	unsigned failures = 0;
	unsigned count = 0;

	for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
		bool ok = run_scenario_child(scenarios[i].scenario, scenarios[i].label);

		count++;
		if (ok) {
			printf("  ok   %s\n", scenarios[i].label);
		} else {
			failures++;
			printf("  FAIL %s\n", scenarios[i].label);
		}
	}

	printf("RESULT: %s (%u scenarios)\n", failures == 0 ? "PASS" : "FAIL", count);
	return failures == 0 ? 0 : 1;
}
