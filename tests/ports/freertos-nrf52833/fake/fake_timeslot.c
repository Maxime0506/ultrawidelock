#include <mpsl_timeslot.h>

#include <hal/nrf_timer.h>

#include <stddef.h>

unsigned fake_timeslot_blocks_before_grant;
bool fake_timeslot_count_set_fails;
bool fake_timeslot_open_fails;
bool fake_timeslot_request_fails;
bool fake_timeslot_never_grants;

unsigned fake_timeslot_grants;
unsigned fake_timeslot_requests;
unsigned fake_timeslot_opens;
unsigned fake_timeslot_closes;
unsigned fake_timeslot_count_sets;
uint32_t fake_timeslot_last_length_us;
uint8_t fake_timeslot_last_request_type;
uint8_t fake_timeslot_last_hfclk;
unsigned fake_timeslot_violations;

static mpsl_timeslot_callback_t s_callback;
static mpsl_timeslot_session_id_t s_session;
static bool s_open;
static bool s_pending;
static bool s_driving;

/*
 * A ceiling on how many slots one operation may consume. Real hardware has no
 * such limit, but a driver that never finishes would hang the test; this turns
 * that into a visible failure.
 */
#define FAKE_TIMESLOT_MAX_GRANTS 100000u

void fake_timeslot_reset(void)
{
	fake_timeslot_blocks_before_grant = 0;
	fake_timeslot_count_set_fails = false;
	fake_timeslot_open_fails = false;
	fake_timeslot_request_fails = false;
	fake_timeslot_never_grants = false;
	fake_timeslot_grants = 0;
	fake_timeslot_requests = 0;
	fake_timeslot_opens = 0;
	fake_timeslot_closes = 0;
	fake_timeslot_count_sets = 0;
	fake_timeslot_last_length_us = 0;
	fake_timeslot_last_request_type = 0xff;
	fake_timeslot_last_hfclk = 0xff;
	fake_timeslot_violations = 0;
	s_callback = NULL;
	s_open = false;
	s_pending = false;
	s_driving = false;
}

int32_t mpsl_timeslot_session_count_set(void *p_mem, uint8_t n_sessions)
{
	fake_timeslot_count_sets++;
	if (p_mem == NULL || n_sessions == 0u) {
		fake_timeslot_violations++;
		return -1;
	}
	/* The API requires the context to be four-byte aligned. */
	if (((uintptr_t)p_mem % 4u) != 0u) {
		fake_timeslot_violations++;
		return -1;
	}
	if (fake_timeslot_count_set_fails) {
		return -1;
	}
	return 0;
}

int32_t mpsl_timeslot_session_open(mpsl_timeslot_callback_t callback,
				   mpsl_timeslot_session_id_t *p_session_id)
{
	fake_timeslot_opens++;
	if (callback == NULL || p_session_id == NULL) {
		fake_timeslot_violations++;
		return -1;
	}
	if (fake_timeslot_open_fails) {
		return -1;
	}
	s_callback = callback;
	s_session = 1;
	s_open = true;
	*p_session_id = s_session;
	return 0;
}

int32_t mpsl_timeslot_session_close(mpsl_timeslot_session_id_t session_id)
{
	fake_timeslot_closes++;
	if (!s_open || session_id != s_session) {
		fake_timeslot_violations++;
		return -1;
	}
	s_open = false;
	s_pending = false;
	if (s_callback != NULL) {
		(void)s_callback(s_session, MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED);
	}
	return 0;
}

/* Deliver one slot and follow whatever the callback asks for next. */
static void drive(void)
{
	unsigned blocks_left = fake_timeslot_blocks_before_grant;

	s_driving = true;
	while (s_pending && s_open) {
		mpsl_timeslot_signal_return_param_t *ret;

		s_pending = false;

		if (blocks_left > 0u) {
			blocks_left--;
			/* The radio took the slot; the driver should ask again. */
			(void)s_callback(s_session, MPSL_TIMESLOT_SIGNAL_BLOCKED);
			continue;
		}
		if (fake_timeslot_never_grants) {
			break;
		}
		if (fake_timeslot_grants >= FAKE_TIMESLOT_MAX_GRANTS) {
			fake_timeslot_violations++;
			break;
		}

		/* MPSL resets TIMER0 at the start of every timeslot. */
		fake_timer0.counter_us = 0;
		fake_timer0.cc[NRF_TIMER_CC_CHANNEL0] = 0;
		fake_timeslot_grants++;

		ret = s_callback(s_session, MPSL_TIMESLOT_SIGNAL_START);
		if (ret == NULL) {
			fake_timeslot_violations++;
			break;
		}
		if (ret->callback_action == MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST) {
			if (ret->params.request.p_next == NULL) {
				fake_timeslot_violations++;
				break;
			}
			s_pending = true;
			continue;
		}
		if (ret->callback_action == MPSL_TIMESLOT_SIGNAL_ACTION_END) {
			/* No more requests, so the session falls idle. */
			(void)s_callback(s_session, MPSL_TIMESLOT_SIGNAL_SESSION_IDLE);
			break;
		}
		fake_timeslot_violations++;
		break;
	}
	s_driving = false;
}

int32_t mpsl_timeslot_request(mpsl_timeslot_session_id_t session_id,
			      mpsl_timeslot_request_t const *p_request)
{
	fake_timeslot_requests++;
	if (!s_open || session_id != s_session || p_request == NULL) {
		fake_timeslot_violations++;
		return -1;
	}

	fake_timeslot_last_request_type = p_request->request_type;
	if (p_request->request_type == MPSL_TIMESLOT_REQ_TYPE_EARLIEST) {
		fake_timeslot_last_length_us = p_request->params.earliest.length_us;
		fake_timeslot_last_hfclk = p_request->params.earliest.hfclk;
		if (p_request->params.earliest.length_us > MPSL_TIMESLOT_LENGTH_MAX_US ||
		    p_request->params.earliest.timeout_us > MPSL_TIMESLOT_EARLIEST_TIMEOUT_MAX_US) {
			fake_timeslot_violations++;
			return -1;
		}
	}

	if (fake_timeslot_request_fails) {
		return -1;
	}

	s_pending = true;
	/*
	 * A request issued from inside the callback is picked up by the loop
	 * that is already running, exactly as MPSL would queue it, rather than
	 * recursing.
	 */
	if (!s_driving) {
		drive();
	}
	return 0;
}
