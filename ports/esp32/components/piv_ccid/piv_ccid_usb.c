#include "piv_ccid_usb.h"

#include "piv_ccid.h"
#include "piv_identity.h"

#include <stdio.h>
#include <string.h>

#define PIV_TINYUSB_TASK_STACK_SIZE 8192

#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#define CCID_EP_OUT 0x01u
#define CCID_EP_IN 0x81u
#define CCID_EP_NOTIFY 0x82u
#define CCID_EP_SIZE 64u
#define CCID_NOTIFY_SIZE 8u

#define CCID_CTRL_ABORT 0x01u
#define CCID_CTRL_GET_CLOCK_FREQUENCIES 0x02u
#define CCID_CTRL_GET_DATA_RATES 0x03u

#define CCID_INTERFACE 0u
#define CCID_INTERFACE_STRING 4u

/*
 * The prototype uses Espressif's VID and a development PID in the range used by
 * their TinyUSB examples. A production device needs an assigned product ID.
 */
static const tusb_desc_device_t s_device_descriptor = {
	.bLength = sizeof(tusb_desc_device_t),
	.bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = TUSB_CLASS_UNSPECIFIED,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor = TINYUSB_ESPRESSIF_VID,
	.idProduct = 0x40c1,
	.bcdDevice = 0x0100,
	.iManufacturer = 0x01,
	.iProduct = 0x02,
	.iSerialNumber = 0x03,
	.bNumConfigurations = 0x01,
};

/*
 * USB CCID 1.1, one slot, T=1, short-APDU exchange. The descriptor advertises
 * automatic voltage/clock/baud/parameter/IFSD handling and a maximum complete
 * CCID message equal to the bounded protocol-core buffer.
 */
static const uint8_t s_configuration_descriptor[] = {
	/* Configuration: one interface, bus-powered, 100 mA. */
	0x09, TUSB_DESC_CONFIGURATION, 0x5d, 0x00,
	0x01, 0x01, 0x00, 0x80, 0x32,

	/* CCID interface with bulk OUT, bulk IN, and slot-change interrupt IN. */
	0x09, TUSB_DESC_INTERFACE,
	CCID_INTERFACE, 0x00, 0x03,
	TUSB_CLASS_SMART_CARD, 0x00, 0x00, CCID_INTERFACE_STRING,

	/* CCID functional descriptor. */
	0x36, PIV_CCID_FUNCTIONAL_DESCRIPTOR_TYPE,
	0x10, 0x01,             /* bcdCCID 1.10 */
	0x00,                   /* bMaxSlotIndex */
	0x07,                   /* 5 V, 3 V, 1.8 V */
	0x02, 0x00, 0x00, 0x00, /* T=1 */
	0xa0, 0x0f, 0x00, 0x00, /* default clock: 4000 kHz */
	0xa0, 0x0f, 0x00, 0x00, /* maximum clock: 4000 kHz */
	0x00,                   /* query supported clock list */
	0x80, 0x25, 0x00, 0x00, /* default data rate: 9600 bps */
	0x90, 0xd0, 0x03, 0x00, /* maximum data rate: 250000 bps */
	0x00,                   /* query supported data-rate list */
	0xfe, 0x00, 0x00, 0x00, /* maximum IFSD */
	0x00, 0x00, 0x00, 0x00, /* synchronous protocols */
	0x00, 0x00, 0x00, 0x00, /* mechanical features */
	0xba, 0x04, 0x02, 0x00, /* automatic features + short APDU */
	0x0a, 0x04, 0x00, 0x00, /* maximum CCID message: 1034 bytes */
	0x00,                   /* bClassGetResponse */
	0x00,                   /* bClassEnvelope */
	0x00, 0x00,             /* no LCD */
	0x00,                   /* no reader-managed PIN pad */
	0x01,                   /* one busy slot */

	/* Bulk OUT. */
	0x07, TUSB_DESC_ENDPOINT, CCID_EP_OUT, TUSB_XFER_BULK,
	0x40, 0x00, 0x00,
	/* Bulk IN. */
	0x07, TUSB_DESC_ENDPOINT, CCID_EP_IN, TUSB_XFER_BULK,
	0x40, 0x00, 0x00,
	/* Interrupt IN for slot-change notification. */
	0x07, TUSB_DESC_ENDPOINT, CCID_EP_NOTIFY, TUSB_XFER_INTERRUPT,
	CCID_NOTIFY_SIZE, 0x00, 0x10,
};

static char s_serial_string[13];
static const char *s_string_descriptors[] = {
	(char[]){0x09, 0x04},
	"OpenAliro",
	"Presence PIV Token (VM Bench)",
	s_serial_string,
	"PIV CCID",
};

struct ccid_usb {
	struct piv_ccid protocol;
	uint8_t rhport;
	uint8_t ep_out;
	uint8_t ep_in;
	uint8_t ep_notify;
};

static struct ccid_usb s_usb;

CFG_TUD_MEM_SECTION static uint8_t s_rx[PIV_CCID_MAX_MESSAGE]
	CFG_TUD_MEM_ALIGN;
CFG_TUD_MEM_SECTION static uint8_t s_tx[PIV_CCID_MAX_MESSAGE]
	CFG_TUD_MEM_ALIGN;

static void ccid_init(void)
{
	memset(&s_usb, 0, sizeof(s_usb));
	piv_ccid_init(&s_usb.protocol, piv_identity_backend(), NULL, true);
}

static bool ccid_deinit(void)
{
	ccid_init();
	return true;
}

static void ccid_reset(uint8_t rhport)
{
	ccid_init();
	s_usb.rhport = rhport;
}

static uint16_t ccid_open(uint8_t rhport,
			  const tusb_desc_interface_t *interface,
			  uint16_t max_len)
{
	if (interface == NULL ||
	    interface->bInterfaceClass != TUSB_CLASS_SMART_CARD ||
	    interface->bInterfaceNumber != CCID_INTERFACE) {
		return 0;
	}

	const uint8_t *start = (const uint8_t *)interface;
	const uint8_t *end = start + max_len;
	const uint8_t *descriptor = tu_desc_next(interface);

	s_usb.rhport = rhport;
	while (tu_desc_in_bounds(descriptor, end)) {
		uint8_t type = tu_desc_type(descriptor);
		if (type == TUSB_DESC_INTERFACE ||
		    type == TUSB_DESC_INTERFACE_ASSOCIATION) {
			break;
		}
		if (type == TUSB_DESC_ENDPOINT) {
			const tusb_desc_endpoint_t *endpoint =
				(const tusb_desc_endpoint_t *)descriptor;
			if (!usbd_edpt_open(rhport, endpoint)) {
				return 0;
			}
			uint8_t address = endpoint->bEndpointAddress;
			uint8_t transfer = descriptor[3] & 0x03u;
			if ((address & TUSB_DIR_IN_MASK) == 0u) {
				s_usb.ep_out = address;
			} else if (transfer == TUSB_XFER_INTERRUPT) {
				s_usb.ep_notify = address;
			} else {
				s_usb.ep_in = address;
			}
		}
		descriptor = tu_desc_next(descriptor);
	}

	if (s_usb.ep_out == 0u || s_usb.ep_in == 0u ||
	    s_usb.ep_notify == 0u ||
	    !usbd_edpt_xfer(rhport, s_usb.ep_out, s_rx, sizeof(s_rx), false)) {
		return 0;
	}
	return (uint16_t)(descriptor - start);
}

static bool ccid_control(uint8_t rhport, uint8_t stage,
			 const tusb_control_request_t *request)
{
	static const uint8_t clock_khz[] = {0xa0, 0x0f, 0x00, 0x00};
	static const uint8_t data_rate[] = {0x90, 0xd0, 0x03, 0x00};

	if (stage != CONTROL_STAGE_SETUP) {
		return true;
	}
	if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS ||
	    request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
	    (uint8_t)request->wIndex != CCID_INTERFACE) {
		return false;
	}

	switch (request->bRequest) {
	case CCID_CTRL_ABORT:
		return request->bmRequestType_bit.direction == TUSB_DIR_OUT &&
		       request->wLength == 0u &&
		       tud_control_status(rhport, request);
	case CCID_CTRL_GET_CLOCK_FREQUENCIES:
		return request->bmRequestType_bit.direction == TUSB_DIR_IN &&
		       tud_control_xfer(rhport, request, (void *)clock_khz,
					sizeof(clock_khz));
	case CCID_CTRL_GET_DATA_RATES:
		return request->bmRequestType_bit.direction == TUSB_DIR_IN &&
		       tud_control_xfer(rhport, request, (void *)data_rate,
					sizeof(data_rate));
	default:
		return false;
	}
}

static bool ccid_transfer(uint8_t rhport, uint8_t endpoint,
			  xfer_result_t result, uint32_t transferred)
{
	if (result != XFER_RESULT_SUCCESS) {
		if (endpoint == s_usb.ep_out) {
			return usbd_edpt_xfer(rhport, s_usb.ep_out, s_rx,
					      sizeof(s_rx), false);
		}
		return true;
	}

	if (endpoint == s_usb.ep_out) {
		size_t response_len = 0;
		if (piv_ccid_process(&s_usb.protocol, s_rx, transferred,
				     s_tx, sizeof(s_tx), &response_len) != 0) {
			return usbd_edpt_xfer(rhport, s_usb.ep_out, s_rx,
					      sizeof(s_rx), false);
		}
		return usbd_edpt_xfer(rhport, s_usb.ep_in, s_tx,
				      (uint16_t)response_len, false);
	}
	if (endpoint == s_usb.ep_in) {
		return usbd_edpt_xfer(rhport, s_usb.ep_out, s_rx,
				      sizeof(s_rx), false);
	}
	return endpoint == s_usb.ep_notify;
}

static const usbd_class_driver_t s_ccid_driver = {
	.name = "PIV CCID",
	.init = ccid_init,
	.deinit = ccid_deinit,
	.reset = ccid_reset,
	.open = ccid_open,
	.control_xfer_cb = ccid_control,
	.xfer_cb = ccid_transfer,
	.xfer_isr = NULL,
	.sof = NULL,
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count)
{
	if (driver_count == NULL) {
		return NULL;
	}
	*driver_count = 1;
	return &s_ccid_driver;
}

esp_err_t piv_ccid_usb_start(void)
{
	uint8_t mac[6];
	esp_err_t err = piv_identity_init();

	if (err != ESP_OK) {
		return err;
	}
	err = esp_efuse_mac_get_default(mac);
	if (err != ESP_OK) {
		return err;
	}
	snprintf(s_serial_string, sizeof(s_serial_string),
		 "%02X%02X%02X%02X%02X%02X",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
	/*
	 * macOS pairing performs the presence-gated slot-9A ECDSA operation from
	 * the CCID callback. The PSA/mbedTLS signing path exceeds TinyUSB's 4096
	 * byte default task stack.
	 */
	config.task.size = PIV_TINYUSB_TASK_STACK_SIZE;
	config.descriptor.device = &s_device_descriptor;
	config.descriptor.full_speed_config = s_configuration_descriptor;
	config.descriptor.string = s_string_descriptors;
	config.descriptor.string_count =
		sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]);
	return tinyusb_driver_install(&config);
}
