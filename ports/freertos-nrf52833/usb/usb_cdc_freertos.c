/* SPDX-License-Identifier: ISC */

/*
 * The CDC ACM serial port: one instance, one task, and a line reader.
 *
 * Everything below the class instance is vendor code compiled unmodified --
 * see CMakeLists.txt and usb/usb_compat/. What is here is the product's shape
 * on top of it: which interfaces and endpoints the port takes, how its events
 * reach a task, and the blocking readline the console is written against.
 *
 * WHY A TASK AND NOT A CALLBACK. app_usbd is configured with its event queue
 * enabled (sdk_config.h), so class callbacks do not run in the USBD vector;
 * something has to drain the queue. That something is a task here rather than a
 * timer or an idle hook because the console's own work -- a P-256 derive inside
 * `import` -- runs on it too, and putting that anywhere else would mean a
 * second stack sized for crypto.
 *
 * WHY THE PUMP POLLS. The obvious design signals the task from the USBD vector.
 * It cannot: the POWER half of this stack's events arrives at MPSL's priority 0,
 * which is above the FreeRTOS syscall ceiling, so no FromISR call is legal
 * there. A 5 ms poll is the honest alternative and costs nothing that matters --
 * this runs only in provisioning mode, with the radios deliberately not started,
 * and the thing on the other end is a person typing.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "app_usbd.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_core.h"
#include "app_usbd_serial_num.h"
#include "app_usbd_string_desc.h"

#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_usb.h"

#define TAG "usb_cdc"

/*
 * CDC ACM is two interfaces by specification: a communication interface that
 * carries the notification endpoint, and a data interface with the bulk pair.
 * 0 and 1 because this device has no other class.
 */
#define CDC_ACM_COMM_INTERFACE 0
#define CDC_ACM_DATA_INTERFACE 1

#define CDC_ACM_COMM_EPIN  NRF_DRV_USBD_EPIN2
#define CDC_ACM_DATA_EPIN  NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT NRF_DRV_USBD_EPOUT1

static void cdc_acm_ev_handler(app_usbd_class_inst_t const *p_inst,
			       app_usbd_cdc_acm_user_event_t event);

APP_USBD_CDC_ACM_GLOBAL_DEF(m_cdc_acm, cdc_acm_ev_handler, CDC_ACM_COMM_INTERFACE,
			    CDC_ACM_DATA_INTERFACE, CDC_ACM_COMM_EPIN, CDC_ACM_DATA_EPIN,
			    CDC_ACM_DATA_EPOUT, APP_USBD_CDC_COMM_PROTOCOL_AT_V250);

/*
 * One byte at a time.
 *
 * The class reads into a caller-supplied buffer and completes when it is full,
 * which for a console means a command would not arrive until the operator had
 * typed exactly as many characters as the buffer holds. Asking for one byte
 * makes every keystroke a completion, which is what echo and backspace need.
 * The cost is one USB read request per character on a link that is idle
 * between them.
 */
static char s_rx_byte;

/* Filled by the event handler, drained by readline. Single producer (the pump
 * task) and single consumer (whoever called readline), and both are the same
 * task, so no lock: the handler runs inside app_usbd_event_queue_process(). */
#define RX_RING_SIZE 1024u
static char s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

static volatile bool s_port_open;

#ifndef ULTRAWIDELOCK_FREERTOS_USB_TASK_STACK_BYTES
/*
 * Sized for the console rather than for USB. `import` runs
 * ultrawidelock_reader_import_blob, whose commit path does a software P-256 derive --
 * the heaviest single crypto step the reader has -- and it runs on this stack.
 */
#define ULTRAWIDELOCK_FREERTOS_USB_TASK_STACK_BYTES 4096u
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_USB_TASK_PRIORITY
#define ULTRAWIDELOCK_FREERTOS_USB_TASK_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#define USB_STACK_WORDS                                                                            \
	((ULTRAWIDELOCK_FREERTOS_USB_TASK_STACK_BYTES + sizeof(StackType_t) - 1u) / sizeof(StackType_t))

static TaskHandle_t s_task;
static StaticTask_t s_task_storage;
static StackType_t s_stack[USB_STACK_WORDS];

static void rx_push(char c)
{
	uint16_t next = (uint16_t)((s_rx_head + 1u) % RX_RING_SIZE);

	if (next == s_rx_tail) {
		/* Full. Dropping the newest rather than the oldest keeps the
		 * partial line the operator can see on their terminal matching
		 * what this side holds -- overwriting the oldest would silently
		 * change the beginning of a command already echoed. */
		return;
	}
	s_rx_ring[s_rx_head] = c;
	s_rx_head = next;
}

static bool rx_pop(char *out)
{
	if (s_rx_tail == s_rx_head) {
		return false;
	}
	*out = s_rx_ring[s_rx_tail];
	s_rx_tail = (uint16_t)((s_rx_tail + 1u) % RX_RING_SIZE);
	return true;
}

static void cdc_acm_ev_handler(app_usbd_class_inst_t const *p_inst,
			       app_usbd_cdc_acm_user_event_t event)
{
	app_usbd_cdc_acm_t const *p_cdc_acm = app_usbd_cdc_acm_class_get(p_inst);

	switch (event) {
	case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
		s_port_open = true;
		/* The first read has to be armed here. Until one is outstanding
		 * the class never reports RX_DONE, so a port opened and never
		 * read from looks identical to a silent operator. */
		(void)app_usbd_cdc_acm_read(&m_cdc_acm, &s_rx_byte, 1);
		break;

	case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
		s_port_open = false;
		break;

	case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
		/*
		 * Drain and re-arm in a loop, not once. The class can have more
		 * than one byte buffered from a single USB packet -- a paste is
		 * up to 64 -- and returning after one would leave the rest
		 * sitting until the next keystroke.
		 */
		do {
			rx_push(s_rx_byte);
		} while (app_usbd_cdc_acm_read(&m_cdc_acm, &s_rx_byte, 1) == NRF_SUCCESS);
		break;

	case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
	default:
		break;
	}
	(void)p_cdc_acm;
}

static void usbd_ev_handler(app_usbd_event_type_t event)
{
	switch (event) {
	case APP_USBD_EVT_DRV_SUSPEND:
		app_usbd_suspend_req();
		break;
	case APP_USBD_EVT_POWER_DETECTED:
		if (!nrf_drv_usbd_is_enabled()) {
			app_usbd_enable();
		}
		break;
	case APP_USBD_EVT_POWER_REMOVED:
		app_usbd_stop();
		break;
	case APP_USBD_EVT_POWER_READY:
		/* Only here, never on DETECTED. The 3.3 V regulator has to have
		 * settled before the peripheral is started, and the gap between
		 * the two events is how long VBUS took to rise -- which is why
		 * skipping this works on a bench supply and fails on a laptop. */
		app_usbd_start();
		break;
	default:
		break;
	}
}

/* See the header note: polled, because the POWER events arrive at a priority
 * from which no FreeRTOS API may be called. */
static void usb_task(void *arg)
{
	(void)arg;
	for (;;) {
		while (app_usbd_event_queue_process()) {
			/* Drain fully before sleeping: enumeration is a burst of
			 * events and answering one per tick would make it take
			 * seconds. */
		}
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}

int ultrawidelock_freertos_usb_start(void)
{
	static const app_usbd_config_t config = {
		.ev_state_proc = usbd_ev_handler,
	};
	ret_code_t rc;

	/*
	 * From FICR, so two boards plugged into the same bench are
	 * distinguishable in the host's device list. Before app_usbd_init,
	 * because the string descriptor is read out of this at enumeration.
	 */
	app_usbd_serial_num_generate();

	rc = app_usbd_init(&config);
	if (rc != NRF_SUCCESS) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "usbd init rc=%u", (unsigned)rc);
		return -1;
	}

	rc = app_usbd_class_append(app_usbd_cdc_acm_class_inst_get(&m_cdc_acm));
	if (rc != NRF_SUCCESS) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "cdc append rc=%u", (unsigned)rc);
		return -1;
	}

	s_task = xTaskCreateStatic(usb_task, "usb", (uint32_t)USB_STACK_WORDS, NULL,
				   ULTRAWIDELOCK_FREERTOS_USB_TASK_PRIORITY, s_stack, &s_task_storage);
	if (s_task == NULL) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "no task");
		return -1;
	}

	/*
	 * Last, and after the task exists. This subscribes to the supply events,
	 * and usb_power_freertos.c replays a cable that is already plugged in --
	 * which is the normal case here, since the operator plugs in before
	 * holding the button through reset. Those replayed events go straight
	 * into the queue, so there has to be something to drain it.
	 */
	rc = app_usbd_power_events_enable();
	if (rc != NRF_SUCCESS) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "power events rc=%u", (unsigned)rc);
		return -1;
	}

	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_INFO, TAG, "USB CDC ACM up; open the serial port");
	return 0;
}

bool ultrawidelock_freertos_usb_ready(void)
{
	return s_port_open;
}

int ultrawidelock_freertos_usb_write(const char *data, size_t len)
{
	if (data == NULL || len == 0u) {
		return 0;
	}
	if (!s_port_open) {
		return -1;
	}
	if (app_usbd_cdc_acm_write(&m_cdc_acm, data, len) != NRF_SUCCESS) {
		return -1;
	}
	/*
	 * Wait for the class to finish with the buffer before returning, because
	 * every caller passes a stack local or a static it is about to reuse.
	 * app_usbd_cdc_acm_write is asynchronous and keeps the pointer.
	 */
	while (app_usbd_cdc_acm_write(&m_cdc_acm, NULL, 0) == NRF_ERROR_BUSY) {
		if (!s_port_open) {
			return -1;
		}
		vTaskDelay(pdMS_TO_TICKS(1));
	}
	return 0;
}

int ultrawidelock_freertos_usb_print(const char *line)
{
	static const char crlf[2] = {'\r', '\n'};

	if (line != NULL && line[0] != '\0' && ultrawidelock_freertos_usb_write(line, strlen(line)) != 0) {
		return -1;
	}
	return ultrawidelock_freertos_usb_write(crlf, sizeof(crlf));
}

int ultrawidelock_freertos_usb_readline(char *out, size_t max)
{
	size_t len = 0;

	if (out == NULL || max == 0u) {
		return -1;
	}

	for (;;) {
		char c;

		if (!s_port_open) {
			return -1;
		}
		if (!rx_pop(&c)) {
			vTaskDelay(pdMS_TO_TICKS(5));
			continue;
		}

		if (c == '\r' || c == '\n') {
			/* Terminals send CR, CRLF or LF depending on which one
			 * the operator happens to have. Any of them ends the
			 * line, and an empty line is a valid answer rather than
			 * something to swallow. */
			(void)ultrawidelock_freertos_usb_print("");
			out[len] = '\0';
			return (int)len;
		}
		if (c == '\b' || c == 0x7f) {
			if (len > 0u) {
				len--;
				/* Erase on the operator's screen too: back up,
				 * overwrite with a space, back up again. A bare
				 * backspace only moves the cursor. */
				(void)ultrawidelock_freertos_usb_write("\b \b", 3);
			}
			continue;
		}
		if (c < 0x20 || c > 0x7e) {
			/* Arrow keys and the rest of the escape sequences land
			 * here. Ignored rather than inserted, because an escape
			 * byte in a hex blob would fail to decode with a message
			 * about hex rather than about the arrow key. */
			continue;
		}

		if (len + 1u >= max) {
			/* One character short of the buffer, so the terminator
			 * always fits. Refused rather than truncated: a hex blob
			 * cut short would decode to a valid-looking shorter blob. */
			continue;
		}
		out[len++] = c;
		(void)ultrawidelock_freertos_usb_write(&c, 1);
	}
}
