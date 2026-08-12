/*
 * The radio startup sequencer, the MPSL worker, and the HCI transport all own
 * one-shot static state, so each failure-injection scenario runs in its own
 * forked process against the recording MPSL and SoftDevice Controller doubles.
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
#include "fake_nrf.h"
#include "fake_sdc.h"
#include "ultrawidelock_freertos_mpsl.h"
#include "ultrawidelock_freertos_nimble_sdc.h"
#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_radio.h"

#include <nimble/transport.h>
#include <nrf_errno.h>
#include <sdc.h>
#include <sdc_hci.h>

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                  \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                               \
			printf("  FAIL %s\n", (label));                                             \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                             \
		}                                                                                  \
	} while (0)

/* BSP hooks the port expects from the board layer. */
static unsigned g_entropy_calls;
static size_t g_entropy_last_length;
static unsigned g_log_calls;
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
	g_entropy_calls++;
	g_entropy_last_length = length;
	memset(buffer, 0x5a, length);
	return 0;
}

int8_t ultrawidelock_freertos_die_temperature_c(void)
{
	return 23;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	(void)reason;
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
	g_log_calls++;
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

static jmp_buf g_dispatch_stop;

static void stop_dispatch(void)
{
	longjmp(g_dispatch_stop, 1);
}

static void pump_task(void (*entry)(void *), void *arg)
{
	fake_notify_block_hook = stop_dispatch;
	if (setjmp(g_dispatch_stop) == 0) {
		entry(arg);
	}
	fake_notify_block_hook = NULL;
}

static unsigned g_cmd_put_calls;
static const uint8_t *g_cmd_put_packet;
static unsigned g_msg_get_calls;

static int32_t test_cmd_put(uint8_t *packet)
{
	g_cmd_put_calls++;
	g_cmd_put_packet = packet;
	return 0;
}

/*
 * The real dispatcher answers most commands itself and hands the resulting
 * event back here before it ever asks the controller, so the sequencer must
 * route reception through this and never through sdc_hci_get directly.
 */
static int32_t test_msg_get(uint8_t *packet, uint8_t *type)
{
	g_msg_get_calls++;
	return sdc_hci_get(packet, type);
}

static const struct ultrawidelock_freertos_radio_dispatcher test_dispatcher = {
	.cmd_put = test_cmd_put,
	.msg_get = test_msg_get,
};

static const struct ultrawidelock_freertos_radio_dispatcher half_dispatcher = {
	.cmd_put = test_cmd_put,
};

static void scenario_success(void)
{
	static const uint8_t command[] = {0x03, 0x0c, 0x00};
	void (*mpsl_entry)(void *);
	void *mpsl_arg;
	void (*hci_entry)(void *);
	void *hci_arg;
	unsigned notifications;
	uint8_t rand_buffer[8];

	CHECK("radio start rejects a missing opcode dispatcher",
	      ultrawidelock_freertos_radio_start(NULL) == -ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_TRANSPORT &&
		      fake_mpsl_init_calls == 0 && !ultrawidelock_freertos_radio_ready());
	CHECK("radio start rejects a dispatcher that cannot return events",
	      ultrawidelock_freertos_radio_start(&half_dispatcher) ==
			      -ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_TRANSPORT &&
		      fake_mpsl_init_calls == 0 && !ultrawidelock_freertos_radio_ready());

	CHECK("radio start brings up MPSL, the controller, and the transport",
	      ultrawidelock_freertos_radio_start(&test_dispatcher) == 0 &&
		      ultrawidelock_freertos_radio_ready());
	mpsl_entry = fake_task_entry;
	mpsl_arg = fake_task_arg;

	CHECK("MPSL runs from the board crystal at the oracle's 50 ppm accuracy",
	      fake_mpsl_init_calls == 1 && fake_mpsl_clock_cfg.source == MPSL_CLOCK_LF_SRC_XTAL &&
		      fake_mpsl_clock_cfg.accuracy_ppm == 50 &&
		      fake_mpsl_clock_cfg.rc_ctiv == 0 && fake_mpsl_clock_cfg.rc_temp_ctiv == 0 &&
		      !fake_mpsl_clock_cfg.skip_wait_lfclk_started &&
		      fake_mpsl_assert_handler != NULL);
	CHECK("MPSL signals low-priority work on SWI5_EGU5",
	      fake_mpsl_low_prio_irq == SWI5_EGU5_IRQn);
	CHECK("the MPSL worker drives mpsl_low_priority_process",
	      ultrawidelock_freertos_mpsl_ready() && fake_mpsl_low_priority_process_calls == 0);
	ultrawidelock_freertos_mpsl_wake();
	pump_task(mpsl_entry, mpsl_arg);
	CHECK("the MPSL worker task runs the pinned low-priority processor",
	      fake_mpsl_low_priority_process_calls == 1);

	CHECK("MPSL owns RADIO, RTC0, TIMER0, and POWER_CLOCK at priority zero",
	      fake_nrf_irq_priority[RADIO_IRQn] == 0 && fake_nrf_irq_priority[RTC0_IRQn] == 0 &&
		      fake_nrf_irq_priority[TIMER0_IRQn] == 0 &&
		      fake_nrf_irq_priority[POWER_CLOCK_IRQn] == 0 &&
		      fake_nrf_irq_enabled[RADIO_IRQn] && fake_nrf_irq_enabled[RTC0_IRQn] &&
		      fake_nrf_irq_enabled[TIMER0_IRQn] && fake_nrf_irq_enabled[POWER_CLOCK_IRQn]);
	CHECK("the low-priority signal stays at a FreeRTOS-callable priority",
	      fake_nrf_irq_priority[SWI5_EGU5_IRQn] == 4 && fake_nrf_irq_enabled[SWI5_EGU5_IRQn]);

	ultrawidelock_freertos_radio_radio_isr();
	ultrawidelock_freertos_radio_rtc0_isr();
	ultrawidelock_freertos_radio_timer0_isr();
	ultrawidelock_freertos_radio_power_clock_isr();
	CHECK("board vectors reach the MPSL interrupt handlers",
	      fake_mpsl_radio_isr_calls == 1 && fake_mpsl_rtc0_isr_calls == 1 &&
		      fake_mpsl_timer0_isr_calls == 1 && fake_mpsl_clock_isr_calls == 1);
	ultrawidelock_freertos_radio_low_priority_isr();
	CHECK("the low-priority vector notifies and yields to the MPSL worker",
	      fake_isr_notify_calls == 1 && fake_isr_yield_calls == 1);

	CHECK("only peripheral GATT and CoC controller features are linked in",
	      fake_sdc_init_calls == 1 && fake_sdc_fault_handler != NULL &&
		      fake_sdc_support_calls == 5 && fake_sdc_support_adv_called &&
		      fake_sdc_support_peripheral_called &&
		      fake_sdc_support_dle_peripheral_called &&
		      fake_sdc_support_le_2m_phy_called &&
		      fake_sdc_support_phy_update_peripheral_called);

	CHECK("the controller is configured for one peripheral link and no central",
	      fake_sdc_cfg_count == 4 &&
		      fake_sdc_cfg_records[0].type == SDC_CFG_TYPE_CENTRAL_COUNT &&
		      fake_sdc_cfg_records[0].cfg.central_count.count == 0 &&
		      fake_sdc_cfg_records[1].type == SDC_CFG_TYPE_PERIPHERAL_COUNT &&
		      fake_sdc_cfg_records[1].cfg.peripheral_count.count == 1 &&
		      fake_sdc_cfg_records[2].type == SDC_CFG_TYPE_ADV_COUNT &&
		      fake_sdc_cfg_records[2].cfg.adv_count.count == 1);
	CHECK("Link Layer buffers carry a full 251-byte data length",
	      fake_sdc_cfg_records[3].type == SDC_CFG_TYPE_BUFFER_CFG &&
		      fake_sdc_cfg_records[3].cfg.buffer_cfg.tx_packet_size == 251 &&
		      fake_sdc_cfg_records[3].cfg.buffer_cfg.rx_packet_size == 251 &&
		      fake_sdc_cfg_records[3].cfg.buffer_cfg.tx_packet_count == 3 &&
		      fake_sdc_cfg_records[3].cfg.buffer_cfg.rx_packet_count == 3);
	CHECK("every configuration write uses the default resource tag",
	      fake_sdc_cfg_records[0].tag == SDC_DEFAULT_RESOURCE_CFG_TAG &&
		      fake_sdc_cfg_records[3].tag == SDC_DEFAULT_RESOURCE_CFG_TAG);

	CHECK("the controller runs from the static, eight-byte-aligned pool",
	      fake_sdc_enable_calls == 1 && fake_sdc_memory != NULL &&
		      ((uintptr_t)fake_sdc_memory % 8u) == 0 &&
		      ultrawidelock_freertos_radio_memory_used() == 3078);

	CHECK("the controller entropy source is registered before enable",
	      fake_sdc_rand_source != NULL && fake_sdc_rand_source->rand_poll != NULL);
	fake_sdc_rand_source->rand_poll(rand_buffer, sizeof(rand_buffer));
	CHECK("controller random requests reach the hardware entropy hook",
	      g_entropy_calls == 1 && g_entropy_last_length == sizeof(rand_buffer));

	ble_transport_ll_init();
	hci_entry = fake_task_entry;
	hci_arg = fake_task_arg;
	notifications = fake_task_notify_calls;
	CHECK("the transport was published with the board's opcode dispatcher",
	      ble_transport_to_ll_cmd_impl((void *)command) == 0 && g_cmd_put_calls == 1);

	if (fake_sdc_callback != NULL) {
		fake_sdc_callback();
	}
	CHECK("the controller host signal wakes the HCI receive task",
	      fake_sdc_callback != NULL && fake_task_notify_calls == notifications + 2);

	CHECK("an idle controller reports no data without faulting",
	      fake_sdc_get_result == -NRF_EAGAIN);
	pump_task(hci_entry, hci_arg);
	CHECK("the receive path drains the controller through the dispatcher",
	      g_msg_get_calls >= 1 && fake_sdc_get_calls == g_msg_get_calls &&
		      g_fatal_calls == 0);

	CHECK("radio start is idempotent", ultrawidelock_freertos_radio_start(&test_dispatcher) == 0 &&
					   fake_mpsl_init_calls == 1 && fake_sdc_enable_calls == 1);
}

static void scenario_stage_failure(int stage)
{
	int expected = -stage;

	switch (stage) {
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_MPSL_INIT:
		fake_mpsl_init_result = -NRF_EINVAL;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_INIT:
		fake_sdc_init_result = -NRF_EPERM;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_SUPPORT:
		fake_sdc_support_helper_result = -NRF_EPERM;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_CFG:
		fake_sdc_cfg_error = -NRF_EINVAL;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_MEMORY:
		fake_sdc_cfg_required_memory = 65536;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_RAND:
		fake_sdc_rand_source_result = -NRF_EPERM;
		break;
	case ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_ENABLE:
		fake_sdc_enable_result = -NRF_ENOMEM;
		break;
	default:
		printf("  FAIL unknown stage %d\n", stage);
		g_failures++;
		return;
	}

	CHECK("a failed startup stage is reported and leaves the radio unready",
	      ultrawidelock_freertos_radio_start(&test_dispatcher) == expected &&
		      !ultrawidelock_freertos_radio_ready() &&
		      ultrawidelock_freertos_radio_memory_used() == 0);

	if (stage == ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_MPSL_INIT) {
		CHECK("a failed MPSL init leaves the shared radio vectors disabled",
		      !fake_nrf_irq_enabled[RADIO_IRQn] && !fake_nrf_irq_enabled[RTC0_IRQn] &&
			      !fake_nrf_irq_enabled[TIMER0_IRQn] &&
			      !fake_nrf_irq_enabled[POWER_CLOCK_IRQn] &&
			      !fake_nrf_irq_enabled[SWI5_EGU5_IRQn]);
	}
}

static int run_scenario(int stage)
{
	fake_freertos_reset();
	fake_nimble_reset();
	fake_nrf_reset();
	fake_sdc_reset();

	if (stage == 0) {
		scenario_success();
	} else {
		scenario_stage_failure(stage);
	}
	return g_failures == 0 ? 0 : 1;
}

/* Each scenario needs pristine one-shot statics, so fork one child per stage. */
static bool run_scenario_child(int stage, const char *label)
{
	pid_t pid;
	int status = 0;

	/* Flush first so the forked buffer copy cannot replay the parent's output. */
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int rc = run_scenario(stage);

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
		int stage;
		const char *label;
	} scenarios[] = {
		{0, "radio startup succeeds and publishes the transport"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_MPSL_INIT, "a rejected MPSL clock config stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_INIT, "a failed sdc_init stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_SUPPORT,
		 "a late feature-support call stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_CFG,
		 "a rejected resource configuration stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_MEMORY,
		 "a controller larger than the static pool stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_RAND,
		 "a missing controller entropy source stops startup"},
		{ULTRAWIDELOCK_FREERTOS_RADIO_STAGE_SDC_ENABLE, "a failed sdc_enable stops startup"},
	};
	unsigned failures = 0;
	unsigned count = 0;

	for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
		bool ok = run_scenario_child(scenarios[i].stage, scenarios[i].label);

		count++;
		if (ok) {
			printf("  ok   %s\n", scenarios[i].label);
		} else {
			failures++;
			printf("  FAIL %s\n", scenarios[i].label);
		}
	}

	if (failures != 0) {
		printf("RESULT: FAIL (%u/%u scenarios)\n", failures, count);
		return 1;
	}
	printf("RESULT: PASS (%u scenarios)\n", count);
	return 0;
}
