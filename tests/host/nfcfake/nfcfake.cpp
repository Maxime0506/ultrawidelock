/* nfcfake — the SPI/GPIO/kernel doubles and the Aliro-stack recorder.
 *
 * C++ because it has to define the stack singleton and the reader-storage
 * namespace the two transports call into; the C driver links against it
 * through the extern "C" surface in nfcfake.h. See that header for what this
 * is worth. */

#include "nfcfake.h"

#include <csetjmp>
#include <cstring>

#include <aliro/aliro.h>
#include <reader_storage/reader.h>
#include <aliro_workqueue/aliro_workqueue.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

struct nfcfake_state nfcfake;

const struct device nfcfake_spi_bus = {"spi1"};
const struct device nfcfake_gpio_port = {"gpio0"};

/* ---- thread escape --------------------------------------------------------
 * An unbounded `for (;;)` cannot be left by returning, so the tick budget is
 * spent by every kernel wait and the last one jumps back to the suite. Only
 * PODs live on the frames unwound here. */
static jmp_buf escape;
static bool escape_armed;
static int tick_budget;
static void (*tick_hook)(int remaining);

static void spend_tick(int64_t advance_ms)
{
	nfcfake.uptime_ms += advance_ms > 0 ? advance_ms : 1;
	if (!escape_armed) {
		return;
	}
	tick_budget--;
	/* The hook runs BEFORE the escape check, so a suite can still act on
	 * the last tick of its budget. */
	if (tick_hook != nullptr) {
		tick_hook(tick_budget);
	}
	if (tick_budget <= 0) {
		escape_armed = false;
		longjmp(escape, 1);
	}
}

void nfcfake_on_tick(void (*hook)(int remaining))
{
	tick_hook = hook;
}

void nfcfake_run_thread(void (*entry)(void *, void *, void *), int ticks)
{
	tick_budget = ticks;
	escape_armed = true;
	if (setjmp(escape) == 0) {
		entry(nullptr, nullptr, nullptr);
	}
	escape_armed = false;
}

/* ---- reset ---------------------------------------------------------------- */

static k_thread_entry_t created_entry;

void nfcfake_reset(void)
{
	std::memset(&nfcfake, 0, sizeof(nfcfake));
	tick_hook = nullptr;
	nfcfake.spi_ready = true;
	nfcfake.gpio_ready = true;
	nfcfake.status_byte = 0x01; /* a frame is ready */
	nfcfake.gpio_get_ret = 1;   /* IRQ asserted */
	nfcfake.identifier_set = true;
	for (size_t i = 0; i < sizeof(nfcfake.identifier); i++) {
		nfcfake.identifier[i] = static_cast<uint8_t>(0xA0 + i);
	}
}

void (*nfcfake_thread_entry(void))(void *, void *, void *)
{
	return created_entry;
}

/* ---- scripted chip frames -------------------------------------------------- */

void nfcfake_push_frame(const uint8_t *bytes, size_t len)
{
	if (nfcfake.frame_count >= NFCFAKE_MAX_FRAMES) {
		return;
	}
	if (len > NFCFAKE_FRAME_BYTES) {
		len = NFCFAKE_FRAME_BYTES;
	}
	std::memcpy(nfcfake.frames[nfcfake.frame_count].bytes, bytes, len);
	nfcfake.frames[nfcfake.frame_count].len = len;
	nfcfake.frame_count++;
}

/* The fixed ACK the chip sends before every response (UM0701-02 6.2.1.6). */
static const uint8_t kAck[] = {0x00, 0x00, 0xff, 0x00, 0xff, 0x00};

/* Build a normal-length PN532 host frame: 00 00 FF LEN LCS TFI DATA... DCS 00 */
static void push_host_frame(const uint8_t *data, size_t len)
{
	uint8_t frame[NFCFAKE_FRAME_BYTES];
	size_t n = 0;
	uint8_t sum = 0;

	frame[n++] = 0x00;
	frame[n++] = 0x00;
	frame[n++] = 0xff;
	frame[n++] = static_cast<uint8_t>(len + 1u);              /* LEN: TFI + data */
	frame[n++] = static_cast<uint8_t>(-(int)(len + 1u) & 0xff); /* LCS */
	frame[n++] = 0xd5;                                        /* TFI: chip -> host */
	sum = 0xd5;
	for (size_t i = 0; i < len; i++) {
		frame[n++] = data[i];
		sum = static_cast<uint8_t>(sum + data[i]);
	}
	frame[n++] = static_cast<uint8_t>(-(int)sum & 0xff); /* DCS */
	frame[n++] = 0x00;
	nfcfake_push_frame(frame, n);
}

void nfcfake_push_response(uint8_t cmd, const uint8_t *payload, size_t len)
{
	uint8_t body[NFCFAKE_FRAME_BYTES];

	nfcfake_push_frame(kAck, sizeof(kAck));
	body[0] = static_cast<uint8_t>(cmd + 1u); /* the chip echoes cmd+1 */
	if (len > sizeof(body) - 1u) {
		len = sizeof(body) - 1u;
	}
	if (payload != nullptr && len > 0U) {
		std::memcpy(body + 1, payload, len);
	}
	push_host_frame(body, len + 1u);
}

void nfcfake_push_exchange(uint8_t status, const uint8_t *payload, size_t len)
{
	uint8_t body[NFCFAKE_FRAME_BYTES];

	nfcfake_push_frame(kAck, sizeof(kAck));
	body[0] = 0x41; /* InDataExchange response code */
	body[1] = status;
	if (len > sizeof(body) - 2u) {
		len = sizeof(body) - 2u;
	}
	if (payload != nullptr && len > 0U) {
		std::memcpy(body + 2, payload, len);
	}
	push_host_frame(body, len + 2u);
}

/* ---- SPI ------------------------------------------------------------------ */

bool spi_is_ready_dt(const struct spi_dt_spec *spec)
{
	(void)spec;
	nfcfake.spi_ready_calls++;
	return nfcfake.spi_ready;
}

int spi_write_dt(const struct spi_dt_spec *spec, const struct spi_buf_set *tx)
{
	(void)spec;
	nfcfake.spi_write_calls++;
	if (nfcfake.write_ret != 0) {
		return nfcfake.write_ret;
	}
	if (tx != nullptr && tx->count > 0U && nfcfake.write_count < NFCFAKE_MAX_WRITES) {
		size_t len = tx->buffers[0].len;

		if (len > NFCFAKE_FRAME_BYTES) {
			len = NFCFAKE_FRAME_BYTES;
		}
		std::memcpy(nfcfake.writes[nfcfake.write_count], tx->buffers[0].buf, len);
		nfcfake.write_len[nfcfake.write_count] = len;
		nfcfake.write_count++;
	}
	return 0;
}

int spi_transceive_dt(const struct spi_dt_spec *spec, const struct spi_buf_set *tx,
		      const struct spi_buf_set *rx)
{
	uint8_t *out;
	size_t cap;
	const uint8_t *command;

	(void)spec;
	nfcfake.transceive_calls++;
	if (nfcfake.transceive_ret != 0) {
		return nfcfake.transceive_ret;
	}
	if (tx == nullptr || tx->count == 0U || rx == nullptr || rx->count == 0U) {
		return 0;
	}
	command = static_cast<const uint8_t *>(tx->buffers[0].buf);
	out = static_cast<uint8_t *>(rx->buffers[0].buf);
	cap = rx->buffers[0].len;
	std::memset(out, 0, cap);

	/* 0x02 STATREAD answers one status byte after the command byte. */
	if (command[0] == 0x02) {
		if (cap >= 2u) {
			out[1] = nfcfake.status_byte;
		}
		return 0;
	}
	/* 0x03 DATAREAD streams the current frame after the command byte. */
	if (command[0] == 0x03) {
		if (nfcfake.frame_next >= nfcfake.frame_count) {
			nfcfake.frames_exhausted = true;
			return 0; /* all zeros: the parser will reject it */
		}
		const struct nfcfake_frame *frame = &nfcfake.frames[nfcfake.frame_next++];
		size_t len = frame->len;

		if (len > cap - 1u) {
			len = cap - 1u;
		}
		std::memcpy(out + 1, frame->bytes, len);
		return 0;
	}
	return 0;
}

/* ---- GPIO ----------------------------------------------------------------- */

bool gpio_is_ready_dt(const struct gpio_dt_spec *spec)
{
	(void)spec;
	return nfcfake.gpio_ready;
}

int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t flags)
{
	(void)spec;
	(void)flags;
	nfcfake.gpio_configure_calls++;
	return nfcfake.gpio_configure_ret;
}

int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
	(void)spec;
	nfcfake.gpio_get_calls++;
	return nfcfake.gpio_get_ret;
}

int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	(void)spec;
	nfcfake.gpio_set_calls++;
	nfcfake.last_gpio_set_value = value;
	return nfcfake.gpio_set_ret;
}

void gpio_init_callback(struct gpio_callback *callback, gpio_callback_handler_t handler,
			gpio_port_pins_t pin_mask)
{
	callback->handler = handler;
	callback->pin_mask = pin_mask;
}

int gpio_add_callback(const struct device *port, struct gpio_callback *callback)
{
	(void)port;
	nfcfake.gpio_add_callback_calls++;
	if (nfcfake.gpio_add_callback_ret == 0) {
		nfcfake.last_gpio_callback = callback;
	}
	return nfcfake.gpio_add_callback_ret;
}

void nfcfake_fire_irq(void)
{
	struct gpio_callback *cb = nfcfake.last_gpio_callback;

	if (cb != nullptr && cb->handler != nullptr) {
		cb->handler(&nfcfake_gpio_port, cb, cb->pin_mask);
	}
}

int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t flags)
{
	(void)spec;
	(void)flags;
	nfcfake.gpio_interrupt_calls++;
	return nfcfake.gpio_interrupt_configure_ret;
}

/* ---- kernel --------------------------------------------------------------- */

int64_t k_uptime_get(void)
{
	return nfcfake.uptime_ms;
}

void k_msleep(int ms)
{
	nfcfake.msleep_calls++;
	spend_tick(ms);
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	nfcfake.sem_take_calls++;
	if (sem->count > 0U) {
		sem->count--;
		spend_tick(0);
		return 0;
	}
	spend_tick(timeout > 0 ? timeout : 1);
	return -EAGAIN;
}

void k_sem_give(struct k_sem *sem)
{
	nfcfake.sem_give_calls++;
	if (sem->count < sem->limit) {
		sem->count++;
	}
}

void k_sem_reset(struct k_sem *sem)
{
	nfcfake.sem_reset_calls++;
	sem->count = 0;
}

void k_sem_init(struct k_sem *sem, unsigned initial, unsigned maximum)
{
	sem->count = initial;
	sem->limit = maximum;
}

k_tid_t k_thread_create(struct k_thread *thread, void *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay)
{
	(void)stack;
	(void)stack_size;
	(void)p1;
	(void)p2;
	(void)p3;
	(void)prio;
	(void)options;
	(void)delay;
	nfcfake.thread_create_calls++;
	created_entry = entry;
	return thread;
}

void k_thread_name_set(k_tid_t thread, const char *name)
{
	(void)thread;
	nfcfake.thread_name = name;
}

/* ---- Aliro workqueue and stack --------------------------------------------- */

int AliroWorkqueueSubmit(struct k_work *work)
{
	nfcfake.workqueue_submits++;
	if (nfcfake.workqueue_submit_ret != 0) {
		return nfcfake.workqueue_submit_ret;
	}
	/* Inline, not deferred — see aliro_workqueue.h for why. */
	if (work != nullptr && work->handler != nullptr) {
		work->handler(work);
	}
	return 0;
}

namespace Aliro
{

AliroStack &AliroStack::Instance()
{
	static AliroStack instance;

	return instance;
}

AliroError AliroStack::CreateSession(ConnectionHandle handle)
{
	(void)handle;
	nfcfake.create_session_calls++;
	return ALIRO_NO_ERROR;
}

void AliroStack::HandleSessionData(ConnectionHandle handle, Data data)
{
	(void)handle;
	nfcfake.session_data_calls++;
	size_t len = data.mLength;

	if (len > NFCFAKE_FRAME_BYTES) {
		len = NFCFAKE_FRAME_BYTES;
	}
	if (data.mData != nullptr && len > 0U) {
		std::memcpy(nfcfake.last_session_data, data.mData, len);
	}
	nfcfake.last_session_data_len = len;
}

void AliroStack::DestroySession(ConnectionHandle handle)
{
	(void)handle;
	nfcfake.destroy_session_calls++;
}

} // namespace Aliro

namespace DoorLock
{
namespace ReaderStorage
{

bool IsIdentifierSet(void)
{
	return nfcfake.identifier_set;
}

AliroError GetIdentifier(Aliro::Identifier &out)
{
	if (nfcfake.identifier_ret != 0) {
		return ALIRO_ERROR_INTERNAL;
	}
	std::memcpy(out.data(), nfcfake.identifier, out.size());
	return ALIRO_NO_ERROR;
}

} // namespace ReaderStorage
} // namespace DoorLock
