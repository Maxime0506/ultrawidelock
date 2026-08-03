<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/piv_ccid/piv_ccid_usb.c`

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](../ports.esp32.components.piv_ccid.include/piv_ccid.h.md), [`ports/esp32/components/piv_ccid/include/piv_ccid_usb.h`](../ports.esp32.components.piv_ccid.include/piv_ccid_usb.h.md), [`ports/esp32/components/piv_ccid/include/piv_identity.h`](../ports.esp32.components.piv_ccid.include/piv_identity.h.md)

## API

### `struct ccid_usb`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:114`

USB CCID adapter state: holds protocol handler, host port, and three endpoint addresses (OUT, IN,
INTERRUPT).

### `static void ccid_init(void)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:133`

Initialize CCID USB state: clear struct and set up PIV protocol handler with identity backend and
PIN required.

**called by** `ccid_deinit`, `ccid_reset`

### `static uint16_t ccid_open(uint8_t rhport, const tusb_desc_interface_t *interface, uint16_t max_len)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:156`

Open CCID interface: validate interface descriptor, iterate endpoints to locate OUT (command), IN
(response), and INTERRUPT (notification) endpoints, and arm the OUT endpoint for reception.
Returns descriptor length consumed or 0 on failure (missing endpoints or transfer setup failure).

### `static bool ccid_control(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:208`

Handle USB control requests for CCID class: process ABORT, GET_CLOCK_FREQUENCIES, and
GET_DATA_RATES requests. Returns false if request is not CCID-related, true otherwise.

### `static bool ccid_transfer(uint8_t rhport, uint8_t endpoint, xfer_result_t result, uint32_t transferred)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:246`

Handle USB bulk transfer completion: on OUT endpoint success call piv_ccid_process and send
response on IN; on error retry OUT reception; arm next reception on IN completion. Returns true
to keep processing, false to stall.

### `const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:290`

Retrieve the CCID class driver callback table for TinyUSB: caller must provide non-null
driver_count output parameter. Returns pointer to single CCID driver descriptor.

### `esp_err_t piv_ccid_usb_start(void)`
`ports/esp32/components/piv_ccid/piv_ccid_usb.c:304`

Start PIV CCID over USB: initialize identity, extract MAC address, format serial string,
configure and install TinyUSB driver with enlarged task stack (required for PSA/mbedTLS signing
operations). Returns ESP_OK or error code from initialization steps.

<details><summary>Undocumented (2)</summary>

- `ccid_deinit`
- `ccid_reset`

</details>
