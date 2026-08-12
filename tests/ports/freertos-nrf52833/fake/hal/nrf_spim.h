/*
 * Register-level model of the nRF52833 SPIM with EasyDMA, matching the pinned
 * hal/nrf_spim.h surface the port uses.
 *
 * The model refuses to transfer rather than transferring anyway, and counts
 * every refusal, because each rule below is one a driver can break in a way
 * that no host test would otherwise notice:
 *
 *   RAM only          EasyDMA cannot reach flash. A const body buffer handed
 *                     straight to TXD.PTR is not slow, it is a bus fault. This
 *                     is the rule that justifies the port's bounce buffer, so
 *                     it is the rule most worth enforcing.
 *   enabled           TASKS_START against a disabled peripheral does nothing at
 *                     all, and a polled driver then spins forever on END.
 *   END cleared       A driver that starts without clearing END sees the
 *                     previous transfer's event and reads its own request back
 *                     as the response.
 *   SCK connected     The SPIM samples its own clock through the SCK pin's
 *                     input buffer. Configured as a plain output, the bus is
 *                     silent -- and this shows up only on hardware.
 *   pins routed       PSEL left unrouted transfers nothing.
 *   no overlap        TX and RX are read and written concurrently; pointing
 *                     both at one buffer corrupts the data being sent.
 *   MAXCNT width      Sixteen bits on this part. A longer transfer is truncated
 *                     silently, which against this chip means a register read
 *                     that returns the wrong register.
 *
 * What comes back on MISO is whatever fake_spim_respond() has been given. The
 * model is a bus, not a DW3110: it proves the port frames and slices the
 * transaction correctly, which is the port's whole job.
 */
#ifndef TEST_HAL_NRF_SPIM_H
#define TEST_HAL_NRF_SPIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NRF_SPIM_PIN_NOT_CONNECTED 0xFFFFFFFFuL

/* Widest transaction the port may ask for, plus room to catch an overrun. */
#define FAKE_SPIM_BUS_MAX 2048u

typedef enum {
	NRF_SPIM_FREQ_125K = 0x02000000,
	NRF_SPIM_FREQ_250K = 0x04000000,
	NRF_SPIM_FREQ_500K = 0x08000000,
	NRF_SPIM_FREQ_1M = 0x10000000,
	NRF_SPIM_FREQ_2M = 0x20000000,
	NRF_SPIM_FREQ_4M = 0x40000000,
	NRF_SPIM_FREQ_8M = 0x80000000,
	NRF_SPIM_FREQ_16M = 0x0A000000,
	NRF_SPIM_FREQ_32M = 0x14000000,
} nrf_spim_frequency_t;

typedef enum {
	NRF_SPIM_MODE_0 = 0,
	NRF_SPIM_MODE_1,
	NRF_SPIM_MODE_2,
	NRF_SPIM_MODE_3,
} nrf_spim_mode_t;

typedef enum {
	NRF_SPIM_BIT_ORDER_MSB_FIRST = 0,
	NRF_SPIM_BIT_ORDER_LSB_FIRST = 1,
} nrf_spim_bit_order_t;

typedef enum {
	NRF_SPIM_TASK_START = 0x008,
	NRF_SPIM_TASK_STOP = 0x014,
	NRF_SPIM_TASK_SUSPEND = 0x01C,
	NRF_SPIM_TASK_RESUME = 0x020,
} nrf_spim_task_t;

typedef enum {
	NRF_SPIM_EVENT_STOPPED = 0x104,
	NRF_SPIM_EVENT_ENDRX = 0x110,
	NRF_SPIM_EVENT_END = 0x118,
	NRF_SPIM_EVENT_ENDTX = 0x120,
	NRF_SPIM_EVENT_STARTED = 0x14C,
} nrf_spim_event_t;

typedef struct {
	bool enabled;
	uint32_t psel_sck;
	uint32_t psel_mosi;
	uint32_t psel_miso;
	nrf_spim_frequency_t frequency;
	nrf_spim_mode_t mode;
	nrf_spim_bit_order_t bit_order;
	uint8_t orc;

	const uint8_t *tx_ptr;
	size_t tx_maxcnt;
	uint8_t *rx_ptr;
	size_t rx_maxcnt;

	bool event_end;
	bool event_stopped;

	/* Transfers the model actually ran, and bytes clocked in the last one. */
	unsigned transfers;
	size_t last_len;
	/* Every byte the model saw on MOSI, across the whole session. */
	uint8_t mosi[FAKE_SPIM_BUS_MAX];
	size_t mosi_len;

	/* Refusals, by rule. Each is a bug that only hardware would otherwise show. */
	unsigned violations_ram;
	unsigned violations_disabled;
	unsigned violations_stale_end;
	unsigned violations_sck;
	unsigned violations_pins;
	unsigned violations_overlap;
	unsigned violations_maxcnt;

	/*
	 * Set to leave a started transfer unfinished, standing for a peripheral
	 * that never raises END. The port's bounded spin is the only thing
	 * between that and a ranging task that never returns.
	 */
	bool stall;
	unsigned stops;
} fake_spim_t;

extern fake_spim_t fake_spim;
#define NRF_SPIM3 (&fake_spim)

typedef fake_spim_t NRF_SPIM_Type;

void nrf_spim_enable(NRF_SPIM_Type *p_reg);
void nrf_spim_disable(NRF_SPIM_Type *p_reg);
void nrf_spim_pins_set(NRF_SPIM_Type *p_reg, uint32_t sck, uint32_t mosi, uint32_t miso);
void nrf_spim_frequency_set(NRF_SPIM_Type *p_reg, nrf_spim_frequency_t frequency);
void nrf_spim_configure(NRF_SPIM_Type *p_reg, nrf_spim_mode_t mode, nrf_spim_bit_order_t order);
void nrf_spim_orc_set(NRF_SPIM_Type *p_reg, uint8_t orc);
void nrf_spim_tx_buffer_set(NRF_SPIM_Type *p_reg, const uint8_t *buffer, size_t length);
void nrf_spim_rx_buffer_set(NRF_SPIM_Type *p_reg, uint8_t *buffer, size_t length);
void nrf_spim_task_trigger(NRF_SPIM_Type *p_reg, nrf_spim_task_t task);
void nrf_spim_event_clear(NRF_SPIM_Type *p_reg, nrf_spim_event_t event);
bool nrf_spim_event_check(NRF_SPIM_Type *p_reg, nrf_spim_event_t event);

/* ---- test control ------------------------------------------------------- */

void fake_spim_reset(void);

/*
 * The bytes the slave puts on MISO, consumed from the first byte of the next
 * transfer. Shorter than the transfer means the remainder reads as zero, which
 * is what a device that has stopped answering looks like.
 */
void fake_spim_respond(const uint8_t *bytes, size_t length);

/* Total refusals across every rule. */
unsigned fake_spim_violations(void);

/*
 * Declare a buffer to be flash rather than RAM, so that handing its address to
 * EasyDMA is refused. On the part this distinction is the address; on the host
 * every buffer is equally reachable, so the test has to say which one stands
 * for the const body the decadriver passes down.
 */
void fake_spim_mark_flash(const void *ptr, size_t length);

/*
 * The pin the port drives as chip select, so the model can check it is low for
 * the whole transfer. Left unset, CS is not checked.
 */
extern uint32_t fake_spim_cs_pin;
/* False once any transfer ran with CS not asserted. */
extern bool fake_spim_cs_held;

#endif /* TEST_HAL_NRF_SPIM_H */
