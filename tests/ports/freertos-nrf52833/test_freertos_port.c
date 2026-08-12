#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fake_freertos.h"
#include "fake_nimble.h"
#include "fake_nrf.h"
#include "ultrawidelock_log.h"
#include "ultrawidelock_freertos_mpsl.h"
#include "ultrawidelock_freertos_nimble_sdc.h"
#include "ultrawidelock_freertos_openthread.h"
#include "ultrawidelock_osal.h"
#include "ultrawidelock_port.h"

#include <nimble/ble.h>
#include <nimble/transport.h>
#include <syscfg/syscfg.h>
#include <openthread/tasklet.h>
#include <platform/nrf_802154_irq.h>
#include <platform/nrf_802154_random.h>
#include <platform/nrf_802154_temperature.h>

LOG_MODULE_REGISTER(freertos_port_test);

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

static uint64_t g_busy_wait_us;
static unsigned g_log_calls;
static unsigned g_hex_calls;
static enum ultrawidelock_freertos_log_level g_last_log_level;
static const char *g_last_log_tag;
static unsigned g_entropy_calls;
static int8_t g_die_temperature = 23;

int64_t ultrawidelock_freertos_uptime_us(void)
{
	return fake_uptime_us;
}

void ultrawidelock_freertos_busy_wait_us(uint64_t us)
{
	g_busy_wait_us += us;
	fake_uptime_us += (int64_t)us;
}

uint32_t ultrawidelock_freertos_cycle_get_32(void)
{
	return (uint32_t)(fake_uptime_us * 64);
}

int ultrawidelock_freertos_entropy(void *buffer, size_t length)
{
	static const uint32_t seed = 0x12345678u;

	g_entropy_calls++;
	if (length != sizeof(seed)) {
		return -1;
	}
	memcpy(buffer, &seed, sizeof(seed));
	return 0;
}

int8_t ultrawidelock_freertos_die_temperature_c(void)
{
	return g_die_temperature;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	(void)reason;
	abort();
}

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)fmt;
	g_log_calls++;
	g_last_log_level = level;
	g_last_log_tag = tag;
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
	g_hex_calls++;
}

static int g_init_runs;
static int init_hook(void)
{
	g_init_runs++;
	return 0;
}
ULTRAWIDELOCK_INIT_APPLICATION(init_hook);

static jmp_buf g_dispatch_stop;

static void stop_dispatch(void)
{
	longjmp(g_dispatch_stop, 1);
}

static bool g_ot_tasklets_pending;
static unsigned g_ot_tasklet_runs;
static unsigned g_ot_driver_runs;
static unsigned g_mpsl_process_runs;

static void test_mpsl_low_priority_process(void)
{
	g_mpsl_process_runs++;
}

#define TEST_SDC_NO_DATA (-11)
#define TEST_SDC_TYPE_ACL 0x02u
#define TEST_SDC_TYPE_EVT 0x04u
#define TEST_SDC_TYPE_ISO 0x08u

static uint8_t g_sdc_rx_packet[258];
static size_t g_sdc_rx_size;
static uint8_t g_sdc_rx_type;
static bool g_sdc_rx_ready;
static unsigned g_sdc_cmd_calls;
static unsigned g_sdc_data_calls;
static uint8_t g_sdc_tx_packet[258];
static size_t g_sdc_tx_size;
static int32_t g_sdc_cmd_result;
static int32_t g_sdc_data_result;
static unsigned g_sdc_fault_calls;
static enum ultrawidelock_freertos_nimble_sdc_fault g_sdc_last_fault;
static int32_t g_sdc_last_fault_detail;

static int32_t test_sdc_cmd_put(uint8_t *packet)
{
	g_sdc_cmd_calls++;
	g_sdc_tx_size = 3u + packet[2];
	memcpy(g_sdc_tx_packet, packet, g_sdc_tx_size);
	return g_sdc_cmd_result;
}

static int32_t test_sdc_data_put(const uint8_t *packet)
{
	g_sdc_data_calls++;
	g_sdc_tx_size = 4u + packet[2] + ((size_t)packet[3] << 8);
	memcpy(g_sdc_tx_packet, packet, g_sdc_tx_size);
	return g_sdc_data_result;
}

static int32_t test_sdc_msg_get(uint8_t *packet, uint8_t *type)
{
	if (!g_sdc_rx_ready) {
		return TEST_SDC_NO_DATA;
	}
	memcpy(packet, g_sdc_rx_packet, g_sdc_rx_size);
	*type = g_sdc_rx_type;
	g_sdc_rx_ready = false;
	return 0;
}

static void test_sdc_fault(enum ultrawidelock_freertos_nimble_sdc_fault fault, int32_t detail)
{
	g_sdc_fault_calls++;
	g_sdc_last_fault = fault;
	g_sdc_last_fault_detail = detail;
}

static void queue_sdc_packet(uint8_t type, const uint8_t *packet, size_t size)
{
	memcpy(g_sdc_rx_packet, packet, size);
	g_sdc_rx_size = size;
	g_sdc_rx_type = type;
	g_sdc_rx_ready = true;
}

bool otTaskletsArePending(otInstance *instance)
{
	(void)instance;
	return g_ot_tasklets_pending;
}

void otTaskletsProcess(otInstance *instance)
{
	(void)instance;
	g_ot_tasklet_runs++;
	g_ot_tasklets_pending = false;
}

void ultrawidelock_freertos_openthread_process_drivers(otInstance *instance)
{
	(void)instance;
	g_ot_driver_runs++;
}

static void pump_openthread(void (*entry)(void *), void *arg)
{
	fake_notify_block_hook = stop_dispatch;
	if (setjmp(g_dispatch_stop) == 0) {
		entry(arg);
	}
	fake_notify_block_hook = NULL;
}

static void pump_nimble_sdc(void (*entry)(void *), void *arg)
{
	fake_notify_block_hook = stop_dispatch;
	if (setjmp(g_dispatch_stop) == 0) {
		entry(arg);
	}
	fake_notify_block_hook = NULL;
}

static void pump_dispatch(void (*entry)(void *), void *arg)
{
	fake_queue_block_hook = stop_dispatch;
	if (setjmp(g_dispatch_stop) == 0) {
		entry(arg);
	}
	fake_queue_block_hook = NULL;
}

static unsigned g_work_runs;
static void work_handler(struct ultrawidelock_work *work)
{
	(void)work;
	g_work_runs++;
}

static unsigned g_dwork_runs;
static void dwork_handler(struct ultrawidelock_dwork *work)
{
	(void)work;
	g_dwork_runs++;
}

static void thread_entry(void *arg)
{
	*(int *)arg = 1;
}

static void test_port_inlines(void)
{
	uint8_t *zeroed;
	void *overflow;
	ultrawidelock_mutex_t mutex;

	fake_uptime_us = 1234567;
	CHECK("uptime microseconds use the BSP hook", ultrawidelock_uptime_us() == 1234567);
	CHECK("uptime milliseconds derive from the BSP hook", ultrawidelock_uptime_ms() == 1234);
	CHECK("cycle counter uses the BSP hook", ultrawidelock_cycle_get_32() == (uint32_t)(1234567 * 64));

	ultrawidelock_sleep_ms(2);
	CHECK("millisecond sleep yields to FreeRTOS", fake_delay_calls == 1 && fake_delay_ticks == 2);
	ultrawidelock_sleep_ms(0);
	CHECK("zero millisecond sleep is a no-op", fake_delay_calls == 1);
	ultrawidelock_sleep_us(7);
	CHECK("microsecond sleep uses the busy-wait hook", g_busy_wait_us == 7);

	zeroed = ultrawidelock_calloc(4, 8);
	CHECK("calloc uses the FreeRTOS heap and clears memory",
	      zeroed != NULL && zeroed[0] == 0 && zeroed[31] == 0);
	overflow = ultrawidelock_calloc(SIZE_MAX, 2);
	CHECK("calloc rejects multiplication overflow", overflow == NULL && fake_malloc_calls == 1);
	ultrawidelock_free(zeroed);
	CHECK("free returns memory to the FreeRTOS heap", fake_free_calls == 1);

	ultrawidelock_mutex_init(&mutex);
	ultrawidelock_mutex_lock(&mutex);
	ultrawidelock_mutex_unlock(&mutex);
	CHECK("mutex uses a static semaphore", mutex.h == &mutex.buf && mutex.buf.count == 1 &&
					    mutex.buf.takes == 1 && mutex.buf.gives == 1);
}

static void test_osal(void)
{
	void (*dispatch_entry)(void *);
	void *dispatch_arg;
	struct ultrawidelock_work work;
	struct ultrawidelock_dwork dwork;
	ultrawidelock_sem_t sem;
	ULTRAWIDELOCK_THREAD_STACK_DEFINE(stack, 513);
	ultrawidelock_thread_t thread;
	int thread_ran = 0;

	CHECK("OSAL init starts the static dispatcher and runs hooks",
	      ultrawidelock_osal_init_all() == 0 && fake_task_count == 1 && g_init_runs == 1);
	CHECK("OSAL dispatcher uses the configured 4096-byte stack",
	      fake_task_stack_depth * sizeof(StackType_t) == 4096);
	dispatch_entry = fake_task_entry;
	dispatch_arg = fake_task_arg;
	CHECK("OSAL init is idempotent", ultrawidelock_osal_init_all() == 0 && fake_task_count == 1 &&
							 g_init_runs == 1);

	ultrawidelock_work_init(&work, work_handler);
	CHECK("immediate work queues", ultrawidelock_work_submit(&work) == 0);
	CHECK("pending immediate work is queued once", ultrawidelock_work_submit(&work) == 0);
	pump_dispatch(dispatch_entry, dispatch_arg);
	CHECK("dispatcher runs immediate work exactly once", g_work_runs == 1 && !work.pending);

	fake_uptime_us = 10000;
	ultrawidelock_dwork_init(&dwork, dwork_handler);
	CHECK("delayable work arms", ultrawidelock_dwork_schedule(&dwork, 10) == 0);
	CHECK("schedule keeps an existing deadline", ultrawidelock_dwork_schedule(&dwork, 50) == 0 &&
							 dwork.deadline_us == 20000);
	pump_dispatch(dispatch_entry, dispatch_arg);
	CHECK("dispatcher waits until and runs the deadline",
	      g_dwork_runs == 1 && fake_uptime_us == 20000 && !dwork.pending);
	CHECK("reschedule restarts a pending deadline", ultrawidelock_dwork_schedule(&dwork, 20) == 0 &&
							  (fake_uptime_us += 5000) == 25000 &&
							  ultrawidelock_dwork_reschedule(&dwork, 20) == 0 &&
							  dwork.deadline_us == 45000);
	pump_dispatch(dispatch_entry, dispatch_arg);
	CHECK("rescheduled delayable work runs at the new deadline",
	      g_dwork_runs == 2 && fake_uptime_us == 45000 && !dwork.pending);

	CHECK("negative delay is rejected", ultrawidelock_dwork_schedule(&dwork, -1) == -1);
	CHECK("delayable work can be cancelled", ultrawidelock_dwork_schedule(&dwork, 10) == 0 &&
						      ultrawidelock_dwork_cancel(&dwork) == 0 && !dwork.pending);
	pump_dispatch(dispatch_entry, dispatch_arg);
	CHECK("cancelled delayable work does not run", g_dwork_runs == 2);

	ultrawidelock_sem_init(&sem, 0, 1);
	CHECK("empty semaphore does not take", ultrawidelock_sem_take(&sem, 0) == -1);
	ultrawidelock_sem_give(&sem);
	ultrawidelock_sem_give(&sem);
	CHECK("counting semaphore saturates at its limit", ultrawidelock_sem_take(&sem, 0) == 0 &&
							      ultrawidelock_sem_take(&sem, 0) == -1);
	ultrawidelock_sem_give(&sem);
	ultrawidelock_sem_reset(&sem);
	CHECK("semaphore reset drains the count", ultrawidelock_sem_take(&sem, 0) == -1);

	CHECK("static-stack thread creation succeeds",
	      ultrawidelock_thread_create(&thread, stack, ULTRAWIDELOCK_THREAD_STACK_SIZEOF(stack),
					  thread_entry, &thread_ran, ULTRAWIDELOCK_THREAD_PRIO_HIGH,
					  "worker") == 0);
	CHECK("thread priority maps above the OSAL dispatcher",
	      fake_task_priority == tskIDLE_PRIORITY + 3 && fake_task_stack_depth == 129);
	fake_task_entry(fake_task_arg);
	CHECK("recorded thread entry receives its argument", thread_ran == 1);
}

static void test_logging(void)
{
	static const uint8_t bytes[] = {0x01, 0x02};

	LOG_INF("hello %d", 1);
	CHECK("logging passes level and module tag to the BSP", g_log_calls == 1 &&
							    g_last_log_level == ULTRAWIDELOCK_FREERTOS_LOG_INFO &&
							    strcmp(g_last_log_tag, "freertos_port_test") == 0);
	LOG_HEXDUMP_DBG(bytes, sizeof(bytes), "bytes");
	CHECK("hex logging uses the BSP hook", g_hex_calls == 1);
	ultrawidelock_printf("raw");
	CHECK("raw logging uses the BSP hook", g_log_calls == 2 &&
						    g_last_log_level == ULTRAWIDELOCK_FREERTOS_LOG_RAW &&
						    g_last_log_tag == NULL);
}

static void test_openthread_runtime(void)
{
	static otInstance instance = {.test_id = 1u};
	static otInstance other = {.test_id = 2u};
	void (*entry)(void *);
	void *arg;
	unsigned notifications;
	unsigned lock_calls;

	CHECK("OpenThread rejects a null instance", ultrawidelock_freertos_openthread_start(NULL) == -1);
	CHECK("OpenThread starts one static task",
	      ultrawidelock_freertos_openthread_start(&instance) == 0 && fake_task_count == 3 &&
		      fake_task_stack_depth * sizeof(StackType_t) == 4096 &&
		      fake_task_priority == tskIDLE_PRIORITY + 1);
	CHECK("OpenThread publishes the task-owned instance",
	      ultrawidelock_freertos_openthread_instance() == &instance);
	CHECK("OpenThread start is idempotent for the same instance",
	      ultrawidelock_freertos_openthread_start(&instance) == 0 && fake_task_count == 3);
	CHECK("OpenThread refuses a second instance",
	      ultrawidelock_freertos_openthread_start(&other) == -1 && fake_task_count == 3);

	entry = fake_task_entry;
	arg = fake_task_arg;
	g_ot_tasklets_pending = true;
	pump_openthread(entry, arg);
	CHECK("OpenThread task drains tasklets and platform drivers before sleeping",
	      g_ot_tasklet_runs == 1 && g_ot_driver_runs == 1);

	lock_calls = fake_recursive_take_calls;
	ultrawidelock_freertos_openthread_lock();
	ultrawidelock_freertos_openthread_unlock();
	CHECK("external OpenThread calls use the runtime API lock",
	      fake_recursive_take_calls == lock_calls + 1 &&
		      fake_recursive_give_calls == lock_calls + 1);

	notifications = fake_task_notify_calls;
	otTaskletsSignalPending(&other);
	CHECK("tasklets from another instance do not wake the runtime",
	      fake_task_notify_calls == notifications);
	otTaskletsSignalPending(&instance);
	CHECK("pending tasklets wake the OpenThread task",
	      fake_task_notify_calls == notifications + 1);
	ultrawidelock_freertos_openthread_wake_from_isr();
	CHECK("radio and alarm ISRs notify and yield to OpenThread",
	      fake_isr_notify_calls == 1 && fake_isr_yield_calls == 1);
}

static void test_mpsl_runtime(void)
{
	void (*entry)(void *);
	void *arg;
	unsigned lock_calls;
	unsigned notifications;
	unsigned isr_notifications;
	unsigned isr_yields;

	CHECK("MPSL rejects a missing low-priority processor",
	      ultrawidelock_freertos_mpsl_start(NULL) == -1);
	CHECK("MPSL starts one static highest-priority system task",
	      ultrawidelock_freertos_mpsl_start(test_mpsl_low_priority_process) == 0 &&
		      fake_task_count == 4 &&
		      fake_task_stack_depth * sizeof(StackType_t) == 2048 &&
		      fake_task_priority == configMAX_PRIORITIES - 1 &&
		      ultrawidelock_freertos_mpsl_ready());
	CHECK("MPSL start is idempotent for the same processor",
	      ultrawidelock_freertos_mpsl_start(test_mpsl_low_priority_process) == 0 &&
		      fake_task_count == 4);

	entry = fake_task_entry;
	arg = fake_task_arg;
	lock_calls = fake_recursive_take_calls;
	notifications = fake_task_notify_calls;
	ultrawidelock_freertos_mpsl_wake();
	pump_nimble_sdc(entry, arg);
	CHECK("MPSL IRQ work runs in task context under the shared radio lock",
	      fake_task_notify_calls == notifications + 1 && g_mpsl_process_runs == 1 &&
		      fake_recursive_take_calls == lock_calls + 1 &&
		      fake_recursive_give_calls == lock_calls + 1);

	lock_calls = fake_recursive_take_calls;
	ultrawidelock_freertos_mpsl_lock();
	ultrawidelock_freertos_mpsl_unlock();
	CHECK("application radio calls use the same MPSL lock",
	      fake_recursive_take_calls == lock_calls + 1 &&
		      fake_recursive_give_calls == lock_calls + 1);

	isr_notifications = fake_isr_notify_calls;
	isr_yields = fake_isr_yield_calls;
	ultrawidelock_freertos_mpsl_wake_from_isr();
	CHECK("the MPSL low-priority IRQ notifies and yields to its task",
	      fake_isr_notify_calls == isr_notifications + 1 &&
		      fake_isr_yield_calls == isr_yields + 1);
}

static void test_radio_irq_platform(void)
{
	static void (*const isr)(void) = stop_dispatch;

	fake_nrf_reset();
	fake_nrf_irq_enabled[20] = true;
	fake_nrf_irq_pending[20] = true;
	nrf_802154_irq_init(20, 1, isr);
	CHECK("802.15.4 IRQ init disables, clears, and prioritizes the linked vector",
	      !fake_nrf_irq_enabled[20] && !fake_nrf_irq_pending[20] &&
		      fake_nrf_irq_priority[20] == 1 && fake_nrf_irq_disable_calls == 1 &&
		      fake_nrf_irq_clear_calls == 1);
	nrf_802154_irq_enable(20);
	CHECK("802.15.4 IRQ enable state is observable", nrf_802154_irq_is_enabled(20));
	nrf_802154_irq_set_pending(20);
	CHECK("802.15.4 IRQ pending state can be raised", fake_nrf_irq_pending[20]);
	nrf_802154_irq_clear_pending(20);
	nrf_802154_irq_disable(20);
	CHECK("802.15.4 IRQ pending and enable state can be cleared",
	      !fake_nrf_irq_pending[20] && !nrf_802154_irq_is_enabled(20));
	CHECK("802.15.4 IRQ priority reads back from NVIC",
	      nrf_802154_irq_priority_get(20) == 1);
	nrf_802154_irq_init(21, -1, isr);
	CHECK("negative zero-latency priorities map to NVIC priority zero",
	      nrf_802154_irq_priority_get(21) == 0);
}

static void test_radio_misc_platform(void)
{
	uint32_t first;
	uint32_t second;

	nrf_802154_random_init();
	first = nrf_802154_random_get();
	second = nrf_802154_random_get();
	CHECK("802.15.4 PRNG is seeded once from the hardware entropy hook",
	      g_entropy_calls == 1 && first != 0u && second != 0u && first != second);
	nrf_802154_random_deinit();
	nrf_802154_temperature_init();
	CHECK("802.15.4 temperature reads the BSP die sensor",
	      nrf_802154_temperature_get() == g_die_temperature);
	nrf_802154_temperature_deinit();
}

static void test_nimble_sdc_transport(void)
{
	static const struct ultrawidelock_freertos_nimble_sdc_ops ops = {
		.cmd_put = test_sdc_cmd_put,
		.data_put = test_sdc_data_put,
		.msg_get = test_sdc_msg_get,
		.fault = test_sdc_fault,
		.no_data_error = TEST_SDC_NO_DATA,
	};
	static const uint8_t command[] = {0x01, 0x20, 0x02, 0xaa, 0xbb};
	static const uint8_t acl_out[] = {0x01, 0x00, 0x02, 0x00, 0xcc, 0xdd};
	static const uint8_t command_complete[] = {0x0e, 0x04, 0x01, 0x01, 0x20, 0x00};
	static const uint8_t acl_in[] = {0x01, 0x00, 0x03, 0x00, 0x11, 0x22, 0x33};
	static const uint8_t adv_report[] = {0x3e, 0x02, 0x02, 0x00};
	static const uint8_t iso_packet[] = {0x01, 0x00, 0x00, 0x00};
	struct os_mbuf acl;
	struct os_mbuf oversized;
	void (*entry)(void *);
	void *arg;
	unsigned lock_calls;
	unsigned notifications;
	unsigned isr_notifications;
	unsigned isr_yields;

	fake_nimble_reset();
	CHECK("NimBLE/SDC rejects a missing controller contract",
	      ultrawidelock_freertos_nimble_sdc_configure(NULL) == -1);
	CHECK("NimBLE/SDC accepts one complete controller contract",
	      ultrawidelock_freertos_nimble_sdc_configure(&ops) == 0);
	ble_transport_ll_init();
	CHECK("NimBLE/SDC starts one static receive task",
	      fake_task_count == 5 && fake_task_stack_depth * sizeof(StackType_t) == 2048 &&
		      fake_task_priority == tskIDLE_PRIORITY + 2);
	entry = fake_task_entry;
	arg = fake_task_arg;

	lock_calls = fake_recursive_take_calls;
	notifications = fake_task_notify_calls;
	CHECK("HCI commands are serialized, consumed, and wake command-event receive",
	      ble_transport_to_ll_cmd_impl((void *)command) == 0 && g_sdc_cmd_calls == 1 &&
		      g_sdc_tx_size == sizeof(command) &&
		      memcmp(g_sdc_tx_packet, command, sizeof(command)) == 0 &&
		      fake_nimble_buffer_free_calls == 1 &&
		      fake_recursive_take_calls == lock_calls + 1 &&
		      fake_recursive_give_calls == lock_calls + 1 &&
		      fake_task_notify_calls == notifications + 1);

	fake_nimble_mbuf_set(&acl, acl_out, sizeof(acl_out));
	CHECK("outbound ACL is flattened, serialized, and consumed",
	      ble_transport_to_ll_acl_impl(&acl) == 0 && g_sdc_data_calls == 1 &&
		      g_sdc_tx_size == sizeof(acl_out) &&
		      memcmp(g_sdc_tx_packet, acl_out, sizeof(acl_out)) == 0 &&
		      fake_nimble_mbuf_free_calls == 1);
	memset(&oversized, 0, sizeof(oversized));
	oversized.om_packet_len = 256;
	CHECK("outbound ACL larger than the controller limit fails loudly and is consumed",
	      ble_transport_to_ll_acl_impl(&oversized) == BLE_ERR_INV_HCI_CMD_PARMS &&
		      g_sdc_data_calls == 1 && fake_nimble_mbuf_free_calls == 2);
	CHECK("ISO packets are rejected by the non-ISO product configuration",
	      ble_transport_to_ll_iso_impl(&acl) == BLE_ERR_UNSUPPORTED &&
		      fake_nimble_mbuf_free_calls == 3);

	fake_nimble_event_alloc_failures = 1;
	queue_sdc_packet(TEST_SDC_TYPE_EVT, command_complete, sizeof(command_complete));
	pump_nimble_sdc(entry, arg);
	CHECK("required controller events retry until a NimBLE buffer is available",
	      fake_nimble_event_alloc_calls == 2 && fake_nimble_last_event_discardable == 0 &&
		      fake_nimble_host_event_calls == 1 &&
		      fake_nimble_host_packet_size == sizeof(command_complete) &&
		      memcmp(fake_nimble_host_packet, command_complete,
			     sizeof(command_complete)) == 0);

	queue_sdc_packet(TEST_SDC_TYPE_ACL, acl_in, sizeof(acl_in));
	pump_nimble_sdc(entry, arg);
	CHECK("controller ACL data is copied into a NimBLE-owned mbuf",
	      fake_nimble_acl_alloc_calls == 1 && fake_nimble_host_acl_calls == 1 &&
		      fake_nimble_host_packet_size == sizeof(acl_in) &&
		      memcmp(fake_nimble_host_packet, acl_in, sizeof(acl_in)) == 0);

	fake_nimble_event_alloc_failures = 1;
	queue_sdc_packet(TEST_SDC_TYPE_EVT, adv_report, sizeof(adv_report));
	pump_nimble_sdc(entry, arg);
	CHECK("legacy advertising reports use the discardable pool and drop on exhaustion",
	      fake_nimble_last_event_discardable == 1 && fake_nimble_host_event_calls == 1);

	queue_sdc_packet(TEST_SDC_TYPE_ISO, iso_packet, sizeof(iso_packet));
	pump_nimble_sdc(entry, arg);
	CHECK("unexpected controller packet types reach the platform fault policy",
	      g_sdc_fault_calls == 1 && g_sdc_last_fault == ULTRAWIDELOCK_NIMBLE_SDC_FAULT_PACKET &&
		      g_sdc_last_fault_detail == TEST_SDC_TYPE_ISO);

	/*
	 * The event pool block carries no length, so an event longer than one
	 * must be refused before the copy rather than detected afterwards. This
	 * build's controller cannot emit one, which is exactly why the guard has
	 * to be tested with a synthetic packet.
	 */
	{
		uint8_t oversized[MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE) + 4];
		unsigned faults = g_sdc_fault_calls;
		unsigned events = fake_nimble_host_event_calls;
		size_t params = sizeof(oversized) - 2u;

		memset(oversized, 0xa5, sizeof(oversized));
		oversized[0] = 0x0e;
		oversized[1] = (uint8_t)params;
		queue_sdc_packet(TEST_SDC_TYPE_EVT, oversized, sizeof(oversized));
		pump_nimble_sdc(entry, arg);
		CHECK("an event larger than the NimBLE pool block is refused, not copied",
		      g_sdc_fault_calls == faults + 1 &&
			      g_sdc_last_fault == ULTRAWIDELOCK_NIMBLE_SDC_FAULT_PACKET &&
			      g_sdc_last_fault_detail == (int32_t)sizeof(oversized) &&
			      fake_nimble_host_event_calls == events);
	}

	/*
	 * The port's syscfg header has to win over upstream's defaults, and each
	 * of these disagrees with the upstream default on purpose. Reading them
	 * here proves the include order is right in every build that compiles
	 * the transport, not just on the target.
	 */
	CHECK("the port's NimBLE configuration overrides the upstream defaults",
	      MYNEWT_VAL(BLE_ROLE_CENTRAL) == 0 && MYNEWT_VAL(BLE_ROLE_OBSERVER) == 0 &&
		      MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) == 1 && MYNEWT_VAL(BLE_SM_SC) == 1 &&
		      MYNEWT_VAL(BLE_SM_LEGACY) == 0);

	notifications = fake_task_notify_calls;
	ultrawidelock_freertos_nimble_sdc_wake();
	CHECK("task-context controller signals notify the receive task",
	      fake_task_notify_calls == notifications + 1);
	isr_notifications = fake_isr_notify_calls;
	isr_yields = fake_isr_yield_calls;
	ultrawidelock_freertos_nimble_sdc_wake_from_isr();
	CHECK("MPSL ISR signals notify and yield to the receive task",
	      fake_isr_notify_calls == isr_notifications + 1 &&
		      fake_isr_yield_calls == isr_yields + 1);
}

int main(void)
{
	fake_freertos_reset();
	test_port_inlines();
	test_osal();
	test_logging();
	test_openthread_runtime();
	test_mpsl_runtime();
	test_radio_irq_platform();
	test_radio_misc_platform();
	test_nimble_sdc_transport();

	if (g_failures != 0) {
		printf("RESULT: FAIL (%u/%u checks)\n", g_failures, g_checks);
		return 1;
	}
	printf("RESULT: PASS (%u checks)\n", g_checks);
	return 0;
}
