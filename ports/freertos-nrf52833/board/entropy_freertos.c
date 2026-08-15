/* SPDX-License-Identifier: ISC */

/*
 * Hardware entropy on the RNG peripheral.
 *
 * A pool rather than a bare poll, because of who calls this. The SoftDevice
 * Controller registers it as its randomness source and the 802.15.4 driver
 * seeds itself from it, and neither states which context it will ask from. The
 * RNG with bias correction takes on the order of a hundred microseconds per
 * byte, so a caller that blocks for that at a radio interrupt priority would
 * overrun a radio event. Filling a pool from the RNG's own interrupt makes the
 * ordinary request a memcpy and leaves blocking for the case where a caller
 * genuinely outruns the hardware.
 *
 * Bias correction is on. It costs throughput and buys uniformly distributed
 * bits, which is the difference between an entropy source and a noise source;
 * the reader's key material comes from here.
 *
 * When a caller does outrun the pool, it polls the peripheral directly with the
 * RNG's own vector masked, so the handler cannot take the byte it is waiting
 * for. Only that one vector is masked, not interrupts at large, because the
 * wait can be long and the radio must keep running through it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/nrf_rng.h>
#include <nrfx.h>

#include <ultrawidelock_freertos_board.h>
#include <ultrawidelock_freertos_platform.h>

/*
 * Deep enough to answer the largest single request either consumer makes -- a
 * 32-byte key -- without blocking, and shallow enough that keeping it full is
 * not a standing power cost.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES
#define ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES 32u
#endif

static uint8_t s_pool[ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES];
static uint8_t s_head;
static uint8_t s_count;
static bool s_started;
static bool s_generating;

static uint8_t pool_take(void)
{
	uint8_t byte = s_pool[s_head];

	s_head = (uint8_t)((s_head + 1u) % ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES);
	s_count--;
	return byte;
}

static void pool_put(uint8_t byte)
{
	uint8_t tail = (uint8_t)((s_head + s_count) % ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES);

	s_pool[tail] = byte;
	s_count++;
}

/*
 * Run the generator only while the pool has room. Left running it would hold
 * the peripheral busy for no gain, and the RNG is one of the few things on this
 * part that draws current whether or not anyone is listening.
 */
static void generator_set(bool on)
{
	if (on == s_generating) {
		return;
	}
	s_generating = on;
	nrf_rng_task_trigger(NRF_RNG, on ? NRF_RNG_TASK_START : NRF_RNG_TASK_STOP);
}

static void ensure_started(void)
{
	if (s_started) {
		return;
	}
	s_started = true;

	nrf_rng_error_correction_enable(NRF_RNG);
	nrf_rng_event_clear(NRF_RNG, NRF_RNG_EVENT_VALRDY);
	nrf_rng_int_enable(NRF_RNG, NRF_RNG_INT_VALRDY_MASK);
	generator_set(true);
}

void ultrawidelock_freertos_rng_isr(void)
{
	if (!nrf_rng_event_check(NRF_RNG, NRF_RNG_EVENT_VALRDY)) {
		return;
	}
	nrf_rng_event_clear(NRF_RNG, NRF_RNG_EVENT_VALRDY);

	if (s_count < ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES) {
		pool_put(nrf_rng_random_value_get(NRF_RNG));
	}
	if (s_count >= ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES) {
		generator_set(false);
	}
}

/*
 * One byte straight from the peripheral, for a caller that has outrun the pool.
 *
 * The RNG vector is masked across the wait so the handler cannot consume the
 * value first. Everything else stays enabled: this can take a hundred
 * microseconds or more, and the radio has deadlines inside that.
 */
static uint8_t poll_one(void)
{
	uint8_t value;

	NVIC_DisableIRQ(RNG_IRQn);
	generator_set(true);

	while (!nrf_rng_event_check(NRF_RNG, NRF_RNG_EVENT_VALRDY)) {
		/* Spin. The generator is running; this ends. */
	}
	nrf_rng_event_clear(NRF_RNG, NRF_RNG_EVENT_VALRDY);
	value = nrf_rng_random_value_get(NRF_RNG);

	NVIC_EnableIRQ(RNG_IRQn);
	return value;
}

int ultrawidelock_freertos_entropy(void *buffer, size_t length)
{
	uint8_t *out = buffer;
	size_t i;

	if (buffer == NULL && length != 0u) {
		return -1;
	}
	ensure_started();

	for (i = 0; i < length; i++) {
		uint32_t primask = __get_PRIMASK();
		bool took = false;

		/*
		 * The pool is written by the handler and read here, so the test
		 * and the take have to be one step. This is the only interrupt
		 * masking on the fast path and it spans two memory accesses.
		 */
		__disable_irq();
		if (s_count > 0u) {
			out[i] = pool_take();
			took = true;
		}
		__set_PRIMASK(primask);

		if (!took) {
			out[i] = poll_one();
		}
	}

	/* Refill whatever this request drained. */
	if (s_count < ULTRAWIDELOCK_FREERTOS_ENTROPY_POOL_BYTES) {
		generator_set(true);
	}
	return 0;
}
