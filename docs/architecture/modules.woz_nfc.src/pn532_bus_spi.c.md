<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/pn532_bus_spi.c`

Zephyr SPI glue for the PN532 host protocol.
PN532 SPI framing (UM0701-02 §6.2.5): every transaction opens with a
one-byte command — 0x01 DATAWRITE (host→PN532 frame), 0x02 STATREAD (read a
one-byte status; bit0 set = a response frame is ready), 0x03 DATAREAD
(PN532→host frame). The interface is byte-wise LSB-first, which the nRF5340
SPIM does in hardware (SPI_TRANSFER_LSB), so buffers hold ordinary MSB-order
bytes here and the peripheral flips them on the wire.
Each command, status poll, and frame read is its own CS-cycled transaction
(the same shape as the Adafruit/ESPHome PN532 drivers). DATAREAD clocks its
command byte and the complete response through one contiguous SPIM transfer.
The chip re-presents the current frame on each DATAREAD, so reading more bytes
than a frame holds is harmless as long as CS is dropped between frames — with
one exception the caller enforces: the ACK read is kept short
(PN532_ACK_READ_LEN) because the response follows it immediately and a long
over-read would clock it away.
Readiness is polled with STATREAD unless irq-gpios is wired (active low =
frame ready), in which case a GPIO edge wakes the waiting thread.

**depends on** [`modules/woz_nfc/src/pn532_bus.h`](pn532_bus.h.md)  ·  **discussed in** [`modules/woz_nfc/README.md`](../../../modules/woz_nfc/README.md)

## API

### `struct pn532_spi`
`modules/woz_nfc/src/pn532_bus_spi.c:62`

PN532 SPI driver state: SPI bus spec, optional IRQ GPIO, semaphore-gated interrupt, and
pre-allocated frame buffers for SPI read and write. The read_tx/read_rx buffers are kept in a
single descriptor to prevent the nrfx driver from splitting long PN532 responses.

### `static void irq_ready(const struct device *port, struct gpio_callback *callback, gpio_port_pins_t pins)`
`modules/woz_nfc/src/pn532_bus_spi.c:86`

GPIO interrupt handler for PN532 readiness. Fires when the IRQ line goes active and signals a
semaphore to wake the PN532 driver's polling task. Used only if an IRQ GPIO is configured in the
device tree.

### `static int spi_status(struct pn532_spi *c, uint8_t *status)`
`modules/woz_nfc/src/pn532_bus_spi.c:102`

Read the PN532 SPI bus status register and return the device status byte.
Executes a PN532_SPI_STATREAD transaction on the configured SPI bus.
Returns PN532_OK on success and writes the status byte to *status; returns PN532_ERR_IO if the
SPI transceive fails.

**called by** `spi_is_response_ready`

### `static int bus_write(void *ctx, const uint8_t *buf, size_t len)`
`modules/woz_nfc/src/pn532_bus_spi.c:123`

Write bytes to the PN532 NFC controller via SPI. Caller provides a buffer and length; this
function prepends the PN532 DATAWRITE command (0xD5) and transmits via the SPI bus. Returns
PN532_OK on success, PN532_ERR_IO if the buffer exceeds PN532_FRAME_BUF_SIZE or SPI write fails.

### `static bool spi_is_response_ready(struct pn532_spi *c)`
`modules/woz_nfc/src/pn532_bus_spi.c:141`

Poll the PN532 SPI status register and return true if a response is ready (bit 0 set).

**called by** `bus_wait_ready`  ·  **calls** `spi_status`

### `static int bus_wait_ready(void *ctx, int timeout_ms)`
`modules/woz_nfc/src/pn532_bus_spi.c:156`

Wait for the PN532 to be ready: poll the IRQ GPIO if configured (with semaphore fallback), else
poll SPI status. Timeout in milliseconds. Returns PN532_OK on ready, PN532_ERR_TIMEOUT on expiry,
PN532_ERR_IO on GPIO error.

**calls** `spi_is_response_ready`

### `static int bus_read(void *ctx, uint8_t *buf, size_t cap)`
`modules/woz_nfc/src/pn532_bus_spi.c:194`

One contiguous DATAREAD transaction: send the command, clock out `cap` frame
bytes, and drop CS. The chip streams the current frame (00 00 FF …); the
parser in pn532.c locates the start code and validates the length while
tolerating trailing filler from an over-read. `cap` is the caller's read
budget, not a fixed size (see read_frame).

### `void *pn532_bus_ctx(void)`
`modules/woz_nfc/src/pn532_bus_spi.c:228`

Return the SPI bus context for PN532 operations. Caller uses this opaque pointer to route I/O
callbacks to the correct PN532 device instance.

### `int pn532_bus_init(void)`
`modules/woz_nfc/src/pn532_bus_spi.c:239`

Initialize the PN532 SPI bus, GPIO IRQ (if configured), and perform a cold-start wake pulse on
the chip-select line. Logs whether readiness is detected via IRQ or SPI polling. Returns 0 on
success, -1 if SPI/GPIO is not ready or interrupt setup fails. Must be called once before any
PN532 transactions.
