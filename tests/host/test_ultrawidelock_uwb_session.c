/** @file test_ultrawidelock_uwb_session.c — session lifecycle + state machine + dispatch. */
#include <stdlib.h>
#include <string.h>

#include "ultrawidelock_uwb_internal.h" /* struct ultrawidelock_uwb_session, states, internal fns */
#include "ultrawidelock_uwb_msg.h"      /* header accessors */
#include "ultrawidelock_uwb_msg_builder.h"
#include "ultrawidelock_uwb_msg_spec.h"
#include "test.h"

/* --- transmit + event capture ------------------------------------------- */

static struct {
	struct ultrawidelock_uwb_message *msg;
	bool timeout;
	int count;
} g_tx;

static void tx_cb(struct ultrawidelock_uwb_message *m, struct ultrawidelock_uwb_session *s,
		  void *user_data, bool timeout)
{
	(void)s;
	(void)user_data;
	if (g_tx.msg) {
		ultrawidelock_uwb_session_message_free(g_tx.msg);
	}
	g_tx.msg = m;
	g_tx.timeout = timeout;
	g_tx.count++;
}

static struct {
	int count;
	enum ultrawidelock_uwb_session_event_type last_type;
	enum cherry_ccc_session_state last_state;
} g_ev;

static void ev_cb(struct ultrawidelock_uwb_session_event *e, void *user_data)
{
	(void)user_data;
	g_ev.count++;
	g_ev.last_type = e->type;
	if (e->type == ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_STATUS) {
		g_ev.last_state = e->data.status->session_state;
	}
	ultrawidelock_uwb_session_event_free(e);
}

/* --- message helpers ---------------------------------------------------- */

/* Start a message with generous capacity; payload length is fixed up later. */
static struct ultrawidelock_uwb_msg_builder mk(uint8_t proto, uint8_t id)
{
	struct ultrawidelock_uwb_msg_builder b;

	ultrawidelock_uwb_msg_builder_init(&b, 128u);
	ultrawidelock_uwb_msg_builder_header(&b, proto, id, 0u);
	return b;
}

/* Write the real payload length into the header (bytes 2..3, big-endian). */
static void fix_plen(struct ultrawidelock_uwb_message *m)
{
	uint16_t plen = (uint16_t)(m->len - ULTRAWIDELOCK_HEADER_LENGTH);

	m->data[2] = (uint8_t)(plen >> 8);
	m->data[3] = (uint8_t)plen;
}

/* Append the seven attributes an M2 must carry to advance M1_SENT -> M3_SENT. */
static void add_m2_attrs(struct ultrawidelock_uwb_msg_builder *b)
{
	ultrawidelock_uwb_msg_builder_add_u16(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 0x0001u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO, 0x00u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x01u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER, 4u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, 0x01u);
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK, 0x05u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
}

static void add_m4_attrs(struct ultrawidelock_uwb_msg_builder *b)
{
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x1000u);
	ultrawidelock_uwb_msg_builder_add_u64(b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, 0x11223344u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX, 9u);
}

void test_ultrawidelock_uwb_session(void)
{
	const uint32_t SID = 0x11223344u;
	uint16_t pvs[1] = { 0x0100u };
	uint16_t cfgs[1] = { 0x0001u };
	uint8_t combos[1] = { 0x00u };
	uint8_t ursk[32];
	struct cherry_ccc_capabilities ccc;
	struct cherry_core_event_device_capabilities caps;
	struct ultrawidelock_uwb_adapter_reader_config cfg;
	struct ultrawidelock_uwb_msg_builder b;

	memset(&g_tx, 0, sizeof(g_tx));
	memset(&g_ev, 0, sizeof(g_ev));
	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}

	memset(&ccc, 0, sizeof(ccc));
	ccc.protocol_versions.len = 1u;
	ccc.protocol_versions.items = pvs;
	ccc.uwb_configs.len = 1u;
	ccc.uwb_configs.items = cfgs;
	ccc.pulse_shape_combos.len = 1u;
	ccc.pulse_shape_combos.items = combos;
	ccc.minimum_ran_multiplier = 4u;
	ccc.slot_bitmask = 0x01u;
	ccc.channel_bitmask = 0x01u;
	ccc.hopping_config_bitmask = 0x0Au;
	ccc.sync_code_index_bitmask = 0x00000005u;
	memset(&caps, 0, sizeof(caps));
	caps.ccc_capabilities = &ccc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.min_ran_multiplier = 8u;
	cfg.preferred_hopping_configs.count = 1u;
	cfg.preferred_hopping_configs.configs[0] =
		ULTRAWIDELOCK_HOPPING_CONFIG_CONTINUOUS_DEFAULT;
	cfg.r1_antennas[0] = 1u; /* exercise the antenna-set branches in init */
	cfg.r1_antennas[1] = 2u;
	cfg.r2_antennas[0] = 3u;
	cfg.r2_antennas[1] = 4u;

	struct cherry *cx = cherry_create("host", NULL, NULL);
	struct ultrawidelock_uwb_adapter *adapter =
		ultrawidelock_uwb_adapter_create_reader(cx, &caps, &cfg);
	T_OK("adapter", adapter != NULL);

	struct cherry_common_diag_cfg diag; /* exercise the diag branch in init */
	memset(&diag, 0, sizeof(diag));
	diag.aoa = true;
	ultrawidelock_uwb_adapter_set_diagnostics(adapter, diag);

	t_group("create rejects null args");
	T_OK("create.null.ctx",
	     ultrawidelock_uwb_session_create(NULL, SID, ev_cb, tx_cb, NULL) == NULL);
	T_OK("create.null.cb",
	     ultrawidelock_uwb_session_create(adapter, SID, NULL, tx_cb, NULL) == NULL);
	T_OK("create.null.tx",
	     ultrawidelock_uwb_session_create(adapter, SID, ev_cb, NULL, NULL) == NULL);

	struct ultrawidelock_uwb_session *s =
		ultrawidelock_uwb_session_create(adapter, SID, ev_cb, tx_cb, NULL);
	T_OK("create.ok", s != NULL);
	T_EQ("create.state", s->state, CREATED);

	t_group("setters: null guard + happy");
	T_EQ("ursk.null", ultrawidelock_uwb_session_set_ursk(NULL, ursk),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("ursk.null2", ultrawidelock_uwb_session_set_ursk(s, NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("ursk.ok", ultrawidelock_uwb_session_set_ursk(s, ursk), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("pv.null", ultrawidelock_uwb_session_set_protocol_version(NULL, 0x0100u),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pv.ok", ultrawidelock_uwb_session_set_protocol_version(s, 0x0100u),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("to.null", ultrawidelock_uwb_session_set_time_offset(NULL, 0),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("to.ok", ultrawidelock_uwb_session_set_time_offset(s, 0), ULTRAWIDELOCK_UWB_ERR_NONE);

	t_group("suspend/resume/forced reject with no ccc session");
	T_EQ("suspend.null", ultrawidelock_uwb_session_suspend(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("suspend.noccc", ultrawidelock_uwb_session_suspend(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	T_EQ("resume.null", ultrawidelock_uwb_session_resume(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("resume.noccc", ultrawidelock_uwb_session_resume(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	T_EQ("fsuspend.null", ultrawidelock_uwb_session_forced_suspend(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("fsuspend.noccc", ultrawidelock_uwb_session_forced_suspend(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);

	t_group("init_setup builds + transmits M1 (CREATED -> M1_SENT)");
	T_EQ("init.null", ultrawidelock_uwb_session_init_setup(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("init.ok", ultrawidelock_uwb_session_init_setup(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("init.state", s->state, M1_SENT);
	T_OK("m1.captured", g_tx.msg != NULL);
	T_EQ("m1.proto", ultrawidelock_uwb_msg_protocol_header(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE);
	T_EQ("m1.id", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M1);
	T_EQ("m1.timeout", g_tx.timeout, 1);
	T_EQ("init.badstate", ultrawidelock_uwb_session_init_setup(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);

	t_group("message_handle guards + framing");
	T_EQ("mh.null.s", ultrawidelock_uwb_session_message_handle(NULL, g_tx.msg),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("mh.null.m", ultrawidelock_uwb_session_message_handle(s, NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	struct ultrawidelock_uwb_message *shortm = malloc(sizeof(*shortm) + 3u);
	shortm->len = 3u;
	T_EQ("mh.short", ultrawidelock_uwb_session_message_handle(s, shortm),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	free(shortm);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	b.message->data[2] = 0x00u; /* claim payload 10, actual 0 -> mismatch */
	b.message->data[3] = 0x0Au;
	T_EQ("mh.plen", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_AP, 0u);
	fix_plen(b.message);
	T_EQ("mh.unsupproto", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MESSAGE_UNSUPPORTED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("M2 advances to M3_SENT and transmits M3");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2.ok", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("m2.state", s->state, M3_SENT);
	T_EQ("m3.id", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M3);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("M4 brings the session up (M3_SENT -> RANGING, ccc active)");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4.ok", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("m4.state", s->state, RANGING);
	T_OK("m4.ccc", s->ccc_session != NULL);
	T_EQ("m4.active", g_ev.last_state, CHERRY_CCC_SESSION_STATE_ACTIVE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("ccc SESSION_ERROR event -> peer general-error notification");
	/* The shim registers ultrawidelock_ccc_cb as the ccc session's callback (base.cb,
	 * the first member of the ccc session) and invokes it with cherry events.
	 * The host shim only ever emits STATUS events, so drive an ERROR event the
	 * same way the shim would and confirm ultrawidelock_ccc_cb answers with a
	 * general-error notification via the transmit callback. */
	cherry_ccc_cb_t ccc_cb = *(cherry_ccc_cb_t *)s->ccc_session;
	void *ccc_ud = cherry_ccc_session_get_user_data(s->ccc_session);
	struct cherry_ccc_event *ee = malloc(sizeof(*ee));
	ee->type = CHERRY_CCC_EVENT_TYPE_SESSION_ERROR;
	ee->session = s->ccc_session;
	ee->data.error = malloc(sizeof(*ee->data.error));
	ee->data.error->status_err = CHERRY_ERR_INTERNAL;
	ccc_cb(ee, ccc_ud);
	T_EQ("err.notify.proto", ultrawidelock_uwb_msg_protocol_header(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION);
	T_EQ("err.notify.id", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT);
	T_EQ("err.state", s->state, RANGING); /* error does not change state */

	t_group("ccc report/diagnostic/unknown events wrap (or drop) cleanly");
	struct cherry_ccc_event *er = malloc(sizeof(*er));
	er->type = CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT;
	er->session = s->ccc_session;
	er->data.controller_report = NULL; /* pointer is carried, never read */
	ccc_cb(er, ccc_ud);
	T_EQ("ev.controller", g_ev.last_type,
	     ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLLER_REPORT);
	er = malloc(sizeof(*er));
	er->type = CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT;
	er->session = s->ccc_session;
	er->data.controlee_report = NULL;
	ccc_cb(er, ccc_ud);
	T_EQ("ev.controlee", g_ev.last_type,
	     ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT);
	er = malloc(sizeof(*er));
	er->type = CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT;
	er->session = s->ccc_session;
	er->data.diagnostics = NULL;
	ccc_cb(er, ccc_ud);
	T_EQ("ev.diag", g_ev.last_type,
	     ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT);
	er = malloc(sizeof(*er));
	er->type = (enum cherry_ccc_event_type)0x7F;
	er->session = s->ccc_session;
	int before_ev = g_ev.count;
	ccc_cb(er, ccc_ud); /* unknown type: dropped before the client callback */
	T_EQ("ev.unknown.dropped", g_ev.count, before_ev);
	free(er); /* the drop path leaves the cherry event with the caller */

	t_group("graceful suspend (RANGING -> SUSPEND_REQ_SENT)");
	T_EQ("suspend.ok", ultrawidelock_uwb_session_suspend(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("suspend.state", s->state, SUSPEND_REQ_SENT);
	T_EQ("suspend.reqid", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);

	t_group("forced_suspend stops immediately");
	s->state = RANGING;
	T_EQ("fsuspend.ok", ultrawidelock_uwb_session_forced_suspend(s),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("fsuspend.state", s->state, SUSPENDED);

	t_group("resume (SUSPENDED -> RESUME_REQ_SENT)");
	T_EQ("resume.ok", ultrawidelock_uwb_session_resume(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("resume.state", s->state, RESUME_REQ_SENT);
	T_EQ("resume.reqid", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_RESUME_REQUEST);

	t_group("suspend/resume reject a wrong state while ccc is live");
	s->state = RESUME_REQ_SENT; /* not RANGING */
	T_EQ("suspend.badstate", ultrawidelock_uwb_session_suspend(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	s->state = RANGING; /* not SUSPENDED */
	T_EQ("resume.badstate", ultrawidelock_uwb_session_resume(s),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);

	t_group("internal init/start/stop");
	T_EQ("init.null", ultrawidelock_uwb_session_init(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("start.null", ultrawidelock_uwb_session_start(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("stop.null", ultrawidelock_uwb_session_stop(NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("start.ok", ultrawidelock_uwb_session_start(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("stop.ok", ultrawidelock_uwb_session_stop(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("stop.state", s->state, SUSPENDED);

	t_group("start/stop take the close-on-error branch (no ccc session)");
	/* A session with no ccc_session: the cherry call reports an error, so
	 * start/stop run session_close (which frees the session here). */
	struct ultrawidelock_uwb_session *sf1 =
		ultrawidelock_uwb_session_create(adapter, 10u, ev_cb, tx_cb, NULL);
	T_EQ("start.close", ultrawidelock_uwb_session_start(sf1),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	struct ultrawidelock_uwb_session *sf2 =
		ultrawidelock_uwb_session_create(adapter, 11u, ev_cb, tx_cb, NULL);
	T_EQ("stop.close", ultrawidelock_uwb_session_stop(sf2),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);

	t_group("supplementary dispatch through message_handle");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_SUPPLEMENTARY_SERVICE, 0x01u);
	fix_plen(b.message);
	T_EQ("suppl.ok", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("init_setup fails when M1 cannot be built");
	struct ultrawidelock_uwb_session *s5 =
		ultrawidelock_uwb_session_create(adapter, 5u, ev_cb, tx_cb, NULL);
	adapter->ccc_caps.uwb_configs.len = 0u; /* M1's config array turns empty */
	T_EQ("init.m1fail", ultrawidelock_uwb_session_init_setup(s5),
	     ULTRAWIDELOCK_UWB_ERR_INTERNAL);
	adapter->ccc_caps.uwb_configs.len = 1u;
	ultrawidelock_uwb_session_destroy(s5);

	t_group("notification dispatch through message_handle");
	s->state = RANGING;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_RANGING_SUSPENDED,
		0u);
	fix_plen(b.message);
	T_EQ("notif.ok", ultrawidelock_uwb_session_message_handle(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("notif.state", s->state, SUSPENDED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("free helpers tolerate their inputs");
	ultrawidelock_uwb_session_event_free(NULL);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE, 0u);
	fix_plen(b.message);
	ultrawidelock_uwb_session_message_free(b.message);

	t_group("destroy with a live ccc session, then null + no-ccc paths");
	if (g_tx.msg) {
		ultrawidelock_uwb_session_message_free(g_tx.msg);
		g_tx.msg = NULL;
	}
	ultrawidelock_uwb_session_destroy(s);
	ultrawidelock_uwb_session_destroy(NULL);
	struct ultrawidelock_uwb_session *s2 =
		ultrawidelock_uwb_session_create(adapter, 2u, ev_cb, tx_cb, NULL);
	ultrawidelock_uwb_session_destroy(s2); /* no ccc_session -> frees directly */

	t_group("session_init fail path (M4 without a URSK)");
	/* No set_ursk: the ccc session is created but its start fails with
	 * SESSION_CONFIG, so ultrawidelock_uwb_session_init takes goto-fail and frees. */
	struct ultrawidelock_uwb_session *s3 =
		ultrawidelock_uwb_session_create(adapter, 3u, ev_cb, tx_cb, NULL);
	T_EQ("s3.init", ultrawidelock_uwb_session_init_setup(s3), ULTRAWIDELOCK_UWB_ERR_NONE);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("s3.m2", ultrawidelock_uwb_session_message_handle(s3, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	/* init fails -> handle_m4 returns the mapped cherry error; s3 self-frees. */
	T_EQ("s3.m4.fail", ultrawidelock_uwb_session_message_handle(s3, b.message),
	     ULTRAWIDELOCK_UWB_ERR_SESSION_CONFIG);
	ultrawidelock_uwb_msg_free(b.message);
	if (g_tx.msg) {
		ultrawidelock_uwb_session_message_free(g_tx.msg);
		g_tx.msg = NULL;
	}

	ultrawidelock_uwb_adapter_destroy(adapter);
	cherry_destroy_sync(cx);
}
