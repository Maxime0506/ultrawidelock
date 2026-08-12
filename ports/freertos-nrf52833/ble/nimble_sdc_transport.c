#include "ultrawidelock_freertos_nimble_sdc.h"
#include "ultrawidelock_freertos_mpsl.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <nimble/ble.h>
#include <nimble/transport.h>
#include <os/os_mbuf.h>
#include <syscfg/syscfg.h>

#if configSUPPORT_STATIC_ALLOCATION != 1
#error "The NimBLE/SDC transport requires configSUPPORT_STATIC_ALLOCATION=1"
#endif
#if configUSE_TASK_NOTIFICATIONS != 1
#error "The NimBLE/SDC receive pump requires configUSE_TASK_NOTIFICATIONS=1"
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_STACK_BYTES
#define ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_STACK_BYTES 2048u
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_TASK_PRIORITY
#define ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_TASK_PRIORITY (tskIDLE_PRIORITY + 2u)
#endif

#define SDC_STACK_WORDS                                                                 \
	((ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_STACK_BYTES + sizeof(StackType_t) - 1u) /             \
	 sizeof(StackType_t))

/* nrfxlib sdc_hci.h values, checked by freertos-radio-source-check.sh. */
#define SDC_HCI_MSG_TYPE_NONE 0x00u
#define SDC_HCI_MSG_TYPE_DATA 0x02u
#define SDC_HCI_MSG_TYPE_EVT  0x04u
#define SDC_HCI_MSG_TYPE_ISO  0x08u
#define SDC_HCI_PACKET_MAX    258u
#define SDC_HCI_DATA_MAX      255u

#define HCI_EVT_LE_META             0x3eu
#define HCI_SUBEVT_LE_ADV_REPORT    0x02u

enum process_result {
	PROCESS_IDLE,
	PROCESS_PROGRESS,
	PROCESS_RETRY,
};

static struct ultrawidelock_freertos_nimble_sdc_ops s_ops;
static bool s_configured;
static TaskHandle_t s_task;
static StaticTask_t s_task_storage;
static StackType_t s_stack[SDC_STACK_WORDS];
static uint8_t s_rx_packet[SDC_HCI_PACKET_MAX];
static uint8_t s_rx_type;

static void report_fault(enum ultrawidelock_freertos_nimble_sdc_fault fault, int32_t detail)
{
	s_ops.fault(fault, detail);
}

void ultrawidelock_freertos_nimble_sdc_wake(void)
{
	TaskHandle_t task = s_task;

	if (task != NULL) {
		(void)xTaskNotifyGive(task);
	}
}

void ultrawidelock_freertos_nimble_sdc_wake_from_isr(void)
{
	BaseType_t wake = pdFALSE;
	TaskHandle_t task = s_task;

	if (task != NULL) {
		vTaskNotifyGiveFromISR(task, &wake);
		portYIELD_FROM_ISR(wake);
	}
}

static size_t rx_packet_size(uint8_t type)
{
	size_t size;

	if (type == SDC_HCI_MSG_TYPE_EVT) {
		size = 2u + s_rx_packet[1];
	} else if (type == SDC_HCI_MSG_TYPE_DATA) {
		size = 4u + s_rx_packet[2] + ((size_t)s_rx_packet[3] << 8);
	} else {
		return 0u;
	}

	return size <= sizeof(s_rx_packet) ? size : 0u;
}

static bool event_is_discardable(void)
{
	return s_rx_packet[0] == HCI_EVT_LE_META && s_rx_packet[1] >= 1u &&
	       s_rx_packet[2] == HCI_SUBEVT_LE_ADV_REPORT;
}

static enum process_result deliver_event(size_t size)
{
	bool discardable = event_is_discardable();
	void *event;
	int rc;

	/*
	 * The event pool hands out fixed BLE_TRANSPORT_EVT_SIZE blocks with no
	 * length attached, so nothing downstream can catch a copy that is too
	 * long. The configured size fits every event this build's controller can
	 * emit, the largest being the 70-byte Command Complete for Read Local
	 * Supported Commands, but that is a property of the linked feature set
	 * rather than of this function. Check it rather than trust it.
	 */
	if (size > MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)) {
		s_rx_type = SDC_HCI_MSG_TYPE_NONE;
		report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_PACKET, (int32_t)size);
		return PROCESS_PROGRESS;
	}

	event = ble_transport_alloc_evt(discardable ? 1 : 0);
	if (event == NULL) {
		if (discardable) {
			s_rx_type = SDC_HCI_MSG_TYPE_NONE;
			return PROCESS_PROGRESS;
		}
		return PROCESS_RETRY;
	}

	memcpy(event, s_rx_packet, size);
	s_rx_type = SDC_HCI_MSG_TYPE_NONE;
	rc = ble_transport_to_hs_evt(event);
	if (rc != 0) {
		report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_HOST, rc);
	}
	return PROCESS_PROGRESS;
}

static enum process_result deliver_data(size_t size)
{
	struct os_mbuf *data = ble_transport_alloc_acl_from_ll();
	int rc;

	if (data == NULL) {
		return PROCESS_RETRY;
	}
	if (os_mbuf_append(data, s_rx_packet, (uint16_t)size) != 0) {
		(void)os_mbuf_free_chain(data);
		s_rx_type = SDC_HCI_MSG_TYPE_NONE;
		report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_HOST, BLE_ERR_MEM_CAPACITY);
		return PROCESS_PROGRESS;
	}

	s_rx_type = SDC_HCI_MSG_TYPE_NONE;
	rc = ble_transport_to_hs_acl(data);
	if (rc != 0) {
		report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_HOST, rc);
	}
	return PROCESS_PROGRESS;
}

static enum process_result process_one(void)
{
	int32_t rc;
	size_t size;

	if (s_rx_type == SDC_HCI_MSG_TYPE_NONE) {
		ultrawidelock_freertos_mpsl_lock();
		rc = s_ops.msg_get(s_rx_packet, &s_rx_type);
		ultrawidelock_freertos_mpsl_unlock();
		if (rc == s_ops.no_data_error) {
			s_rx_type = SDC_HCI_MSG_TYPE_NONE;
			return PROCESS_IDLE;
		}
		if (rc != 0) {
			s_rx_type = SDC_HCI_MSG_TYPE_NONE;
			report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_GET, rc);
			return PROCESS_IDLE;
		}
	}

	size = rx_packet_size(s_rx_type);
	if (size == 0u || s_rx_type == SDC_HCI_MSG_TYPE_ISO) {
		int32_t detail = s_rx_type;

		s_rx_type = SDC_HCI_MSG_TYPE_NONE;
		report_fault(ULTRAWIDELOCK_NIMBLE_SDC_FAULT_PACKET, detail);
		return PROCESS_PROGRESS;
	}
	if (s_rx_type == SDC_HCI_MSG_TYPE_EVT) {
		return deliver_event(size);
	}
	return deliver_data(size);
}

static void receive_task(void *arg)
{
	(void)arg;
	s_task = xTaskGetCurrentTaskHandle();

	for (;;) {
		enum process_result result = process_one();

		if (result == PROCESS_PROGRESS) {
			continue;
		}
		(void)ulTaskNotifyTake(pdTRUE, result == PROCESS_RETRY ? pdMS_TO_TICKS(1u) :
									  portMAX_DELAY);
	}
}

int ultrawidelock_freertos_nimble_sdc_configure(
	const struct ultrawidelock_freertos_nimble_sdc_ops *ops)
{
	if (ops == NULL || ops->cmd_put == NULL || ops->data_put == NULL ||
	    ops->msg_get == NULL || ops->fault == NULL || !ultrawidelock_freertos_mpsl_ready() ||
	    s_configured) {
		return -1;
	}
	s_ops = *ops;
	s_configured = true;
	return 0;
}

/* Apache NimBLE LL-side transport entry points. */
void ble_transport_ll_init(void)
{
	TaskHandle_t task;

	configASSERT(s_configured);
	if (s_task != NULL) {
		return;
	}
	task = xTaskCreateStatic(receive_task, "nimble-sdc", SDC_STACK_WORDS, NULL,
				 ULTRAWIDELOCK_FREERTOS_NIMBLE_SDC_TASK_PRIORITY, s_stack, &s_task_storage);
	configASSERT(task != NULL);
	s_task = task;
}

int ble_transport_to_ll_cmd_impl(void *buf)
{
	uint8_t *packet = buf;
	int32_t rc;

	if (packet == NULL || !s_configured) {
		return BLE_ERR_INV_HCI_CMD_PARMS;
	}
	if (3u + packet[2] > SDC_HCI_PACKET_MAX) {
		ble_transport_free(buf);
		return BLE_ERR_INV_HCI_CMD_PARMS;
	}

	ultrawidelock_freertos_mpsl_lock();
	rc = s_ops.cmd_put(packet);
	ultrawidelock_freertos_mpsl_unlock();
	ble_transport_free(buf);
	if (rc == 0) {
		ultrawidelock_freertos_nimble_sdc_wake();
		return 0;
	}
	return BLE_ERR_MEM_CAPACITY;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
	uint8_t packet[SDC_HCI_DATA_MAX];
	size_t size;
	int32_t rc;

	if (om == NULL || !s_configured) {
		return BLE_ERR_INV_HCI_CMD_PARMS;
	}
	size = OS_MBUF_PKTLEN(om);
	if (size < 4u || size > sizeof(packet) ||
	    os_mbuf_copydata(om, 0, (int)size, packet) != 0) {
		(void)os_mbuf_free_chain(om);
		return BLE_ERR_INV_HCI_CMD_PARMS;
	}

	ultrawidelock_freertos_mpsl_lock();
	rc = s_ops.data_put(packet);
	ultrawidelock_freertos_mpsl_unlock();
	(void)os_mbuf_free_chain(om);
	if (rc != 0) {
		ultrawidelock_freertos_nimble_sdc_wake();
		return BLE_ERR_MEM_CAPACITY;
	}
	return 0;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{
	if (om != NULL) {
		(void)os_mbuf_free_chain(om);
	}
	return BLE_ERR_UNSUPPORTED;
}
