/* nfcfake — test-side control/inspection API for the Zephyr SPI/GPIO, kernel
 * and Aliro-stack surfaces that modules/woz_nfc builds against.
 *
 * WHAT IS REAL. pn532.c and pn532_apdu.c are the shipping sources, linked in
 * whole: the frame codec, the ACK handshake, ISO-DEP chaining and the APDU
 * adaptation all run for real against a scripted bus. So a transport test that
 * says "one APDU round trip happened" really did drive a PN532 host frame
 * through the real encoder and parse a real response frame back.
 *
 * WHAT IS NOT. There is no SPI peripheral and no chip: nfcfake_spi answers
 * transfers from a scripted response queue, and the GPIO layer is a recorder
 * with knobs. Nothing here shows that a real PN532 answers the way the script
 * says it does. The Aliro stack is a recording double as well — session
 * callbacks are counted, never interpreted.
 *
 * THE POLLING THREAD IS DRIVEN, NOT SCHEDULED. transport_pn532.cpp's thread
 * entry is an unbounded `for (;;)`, so a suite runs it directly and escapes
 * with longjmp() once the tick budget is spent (see nfcfake_run_thread). The
 * frames it unwinds hold only PODs, which is what makes that safe here.
 */
#ifndef WOZ_NFCFAKE_H
#define WOZ_NFCFAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One poll round is nine commands and so eighteen frames (an ACK and a
 * response each); a round plus an exchange and the teardown is more. Sized
 * with room, because a queue that silently overflows looks exactly like a chip
 * that stopped answering. */
#define NFCFAKE_MAX_FRAMES  64
#define NFCFAKE_FRAME_BYTES 300
#define NFCFAKE_MAX_WRITES  64

/** One frame the fake chip will present on the next DATAREAD. */
struct nfcfake_frame {
	uint8_t bytes[NFCFAKE_FRAME_BYTES];
	size_t len;
};

struct nfcfake_state {
	/* ---- SPI ---------------------------------------------------------- */
	bool spi_ready;      /**< spi_is_ready_dt() answer */
	int transceive_ret;  /**< spi_transceive_dt() return */
	int write_ret;       /**< spi_write_dt() return */
	uint8_t status_byte; /**< what STATREAD reports (bit0 = frame ready) */

	/* Frames the chip will hand back, in order. The PN532 protocol reads an
	 * ACK frame and then a response frame per command, so a scripted
	 * transaction is normally two entries. */
	struct nfcfake_frame frames[NFCFAKE_MAX_FRAMES];
	size_t frame_count;
	size_t frame_next;
	bool frames_exhausted;

	/* Last host frames written, so a suite can assert on what went out. */
	uint8_t writes[NFCFAKE_MAX_WRITES][NFCFAKE_FRAME_BYTES];
	size_t write_len[NFCFAKE_MAX_WRITES];
	size_t write_count;

	unsigned transceive_calls, spi_write_calls, spi_ready_calls;

	/* ---- GPIO --------------------------------------------------------- */
	bool gpio_ready;
	int gpio_configure_ret;
	int gpio_set_ret;
	int gpio_get_ret; /**< <0 is an error; otherwise the pin level */
	int gpio_add_callback_ret;
	int gpio_interrupt_configure_ret;
	unsigned gpio_configure_calls, gpio_set_calls, gpio_get_calls;
	unsigned gpio_add_callback_calls, gpio_interrupt_calls;
	int last_gpio_set_value;
	/* The callback the driver registered. Kept so a suite can fire the
	 * edge handler, which is otherwise unreachable: it lives inside the
	 * driver's private state and nothing else calls it. */
	struct gpio_callback *last_gpio_callback;

	/* ---- kernel ------------------------------------------------------- */
	int64_t uptime_ms; /**< advanced by k_msleep and by the tick hook */
	unsigned msleep_calls, sem_take_calls, sem_give_calls, sem_reset_calls;
	unsigned thread_create_calls;
	const char *thread_name;

	/* ---- Aliro stack double ------------------------------------------- */
	unsigned create_session_calls, destroy_session_calls, session_data_calls;
	uint8_t last_session_data[NFCFAKE_FRAME_BYTES];
	size_t last_session_data_len;
	unsigned workqueue_submits;
	int workqueue_submit_ret; /**< nonzero: submission refused */

	/* ---- reader storage double ---------------------------------------- */
	bool identifier_set;
	int identifier_ret;
	uint8_t identifier[8];
};

extern struct nfcfake_state nfcfake;

/** @brief Zero every recording and restore every knob to "works". */
void nfcfake_reset(void);

/** @brief Queue one frame for the chip to present on a later DATAREAD. */
void nfcfake_push_frame(const uint8_t *bytes, size_t len);

/**
 * @brief Queue the standard PN532 ACK followed by a response frame.
 *
 * @p cmd is the command code; the fake echoes cmd+1 as the response code, the
 * way the chip does, and appends @p len payload bytes. This is what makes a
 * scripted transaction one line in a suite.
 */
void nfcfake_push_response(uint8_t cmd, const uint8_t *payload, size_t len);

/** @brief Queue an ACK plus a response carrying an InDataExchange status byte. */
void nfcfake_push_exchange(uint8_t status, const uint8_t *payload, size_t len);

/**
 * @brief Run @p entry (a thread main) for @p ticks kernel waits, then escape.
 *
 * Every k_msleep() and k_sem_take() counts as one tick and advances the fake
 * clock by its timeout. When the budget runs out the fake longjmps back here,
 * which is the only way to leave an unbounded thread loop.
 */
void nfcfake_run_thread(void (*entry)(void *, void *, void *), int ticks);

/**
 * @brief Call @p hook on every tick, with the budget remaining after it.
 *
 * This is how a suite acts on the transport WHILE its thread is inside a wait
 * — which is the only way to reach the session loop, since Send() is refused
 * until the thread has activated a device and the thread cannot be resumed
 * once it has escaped. It also matches how the real thing works: Send() is
 * called from the Aliro workqueue while the polling thread sits on its
 * semaphore. Pass NULL to clear.
 */
void nfcfake_on_tick(void (*hook)(int remaining));

/** @brief The entry function the module handed to k_thread_create(). */
void (*nfcfake_thread_entry(void))(void *, void *, void *);

/** @brief Fire the registered GPIO edge handler, as an IRQ would. */
void nfcfake_fire_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* WOZ_NFCFAKE_H */
