/*
 * Thread/ranging contention gate: measures whether DS-TWR's FINAL->arm margin
 * survives sharing the M4 and 2.4 GHz radio with OpenThread. Deliberately harsh:
 * MED (radio always on), channel 15 on top of BLE ch 38. Never in a shipping
 * image -- CONFIG_ALIRO_THREAD_GATE defaults n and the file compiles out.
 *
 * Needs NET_L2_OPENTHREAD (a "bare OpenThread" build links no radio driver) and
 * CONFIG_OPENTHREAD_MANUAL_START=y so this file can commit its dataset before
 * openthread_run(). Needs an FTD peer on the same dataset (channel 15, PAN
 * 0xf00d, the networkkey below); load is `ot ping async` both directions.
 * Run with CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=8192: at 4096 the ring filled and
 * NO_BLOCK_SKIP silently dropped the grant line.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/openthread.h>

#include <openthread.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/link.h>
#include <openthread/thread.h>

LOG_MODULE_REGISTER(thread_gate, LOG_LEVEL_INF);

/* A fixed dataset, so the board attaches (or searches) without a commissioner
 * in the loop. These are bench values and are not secret: the gate measures
 * radio and CPU contention, and nothing on this network carries traffic worth
 * protecting. A real deployment gets its dataset from Matter's
 * NetworkCommissioning cluster instead. */
#define GATE_CHANNEL   15
#define GATE_PANID     0xf00d
#define GATE_NET_NAME  "ultrawidelock-gate"

static const uint8_t gate_network_key[OT_NETWORK_KEY_SIZE] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

/**
 * OpenThread state change callback. Logs the device role when it changes and, if the node becomes a
 * child or router, logs its mesh-local EID so a remote peer can discover the address to send
 * unicast traffic to.
 */
static void on_state_changed(otChangedFlags flags, void *context)
{
	ARG_UNUSED(context);

	if ((flags & OT_CHANGED_THREAD_ROLE) == 0u) {
		return;
	}

	otInstance *ot = openthread_get_default_instance();
	static const char *const role_name[] = {
		[OT_DEVICE_ROLE_DISABLED] = "disabled",
		[OT_DEVICE_ROLE_DETACHED] = "detached (searching)",
		[OT_DEVICE_ROLE_CHILD] = "child",
		[OT_DEVICE_ROLE_ROUTER] = "router",
		[OT_DEVICE_ROLE_LEADER] = "leader",
	};
	otDeviceRole role = otThreadGetDeviceRole(ot);

	LOG_INF("thread role -> %s", role_name[role] ? role_name[role] : "?");

	/* The peer needs somewhere to send traffic, and this board has no shell in
	 * a gate build (reader + console + Thread overflows RAM), so the address
	 * has to come out of the log. */
	if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER) {
		const otIp6Address *eid = otThreadGetMeshLocalEid(ot);

		LOG_HEXDUMP_INF(eid->mFields.m8, OT_IP6_ADDRESS_SIZE, "mesh-local EID");
	}
}

static struct openthread_state_changed_callback gate_cb = {
	.otCallback = on_state_changed,
};

/* A heartbeat carrying the MAC frame counters, so a walk-up log proves for
 * itself that frames were crossing the radio while ranging ran. Without it the
 * only evidence of load is on the peer, and a gate whose evidence lives on the
 * other board proves nothing about this one. Counters are read rather than
 * timestamps because a live thread with a static rx count is exactly the
 * failure this is meant to catch.
 *
 * Its own thread, not a k_timer: a timer callback runs in ISR context, and
 * CONFIG_LOG_MODE_MINIMAL writes straight through to RTT from wherever it is
 * called. That write next to the DW3110's delayed-TX window is the kind of
 * jitter commit 5b8d06b spent its effort removing. Lowest priority for the same
 * reason -- the gate must never be what breaks ranging. */
#define GATE_BEAT_STACK_SZ 1024
#define GATE_BEAT_MS       5000

K_THREAD_STACK_DEFINE(gate_beat_stack, GATE_BEAT_STACK_SZ);
static struct k_thread gate_beat_thread;

/**
 * Periodic thread status log. Runs every GATE_BEAT_MS milliseconds and logs the OpenThread device
 * role and MAC counters (total RX and TX frames). Runs on a low-priority background thread.
 */
static void gate_beat(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	otInstance *ot = openthread_get_default_instance();

	for (;;) {
		k_msleep(GATE_BEAT_MS);

		openthread_mutex_lock();
		const otMacCounters *mac = otLinkGetCounters(ot);
		uint32_t rx = mac->mRxTotal;
		uint32_t tx = mac->mTxTotal;
		otDeviceRole role = otThreadGetDeviceRole(ot);

		openthread_mutex_unlock();

		LOG_INF("thread alive: role=%d MAC rx=%u tx=%u", (int)role, (unsigned int)rx,
			(unsigned int)tx);
	}
}

/**
 * Start the Thread contention gate. Initializes OpenThread with a fixed operational dataset on the
 * configured channel and PAN ID, brings up the Thread network interface, starts the OpenThread run
 * loop, and spawns a periodic log thread. Returns 0 on success or negative on init, interface, or
 * OpenThread run failure.
 */
static int thread_gate_start(void)
{
	otInstance *ot = openthread_get_default_instance();

	if (ot == NULL) {
		LOG_ERR("no OpenThread instance; is CONFIG_OPENTHREAD_SYS_INIT on?");
		return -ENODEV;
	}

	otOperationalDataset ds;

	memset(&ds, 0, sizeof(ds));
	ds.mChannel = GATE_CHANNEL;
	ds.mComponents.mIsChannelPresent = true;
	ds.mPanId = GATE_PANID;
	ds.mComponents.mIsPanIdPresent = true;
	memcpy(ds.mNetworkKey.m8, gate_network_key, sizeof(gate_network_key));
	ds.mComponents.mIsNetworkKeyPresent = true;
	memcpy(ds.mNetworkName.m8, GATE_NET_NAME, sizeof(GATE_NET_NAME));
	ds.mComponents.mIsNetworkNamePresent = true;

	openthread_mutex_lock();
	otError err = otDatasetSetActive(ot, &ds);

	openthread_mutex_unlock();
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otDatasetSetActive = %d", err);
		return -EIO;
	}

	(void)openthread_state_changed_callback_register(&gate_cb);

	/* Normally already up: the L2 auto-starts the interface and, under
	 * MANUAL_START, that is now a no-op that leaves the interface usable. Doing
	 * it explicitly costs three lines and removes a dependency on that
	 * ordering, which a Kconfig change elsewhere could quietly invert. */
	struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(OPENTHREAD));

	if (iface == NULL) {
		LOG_ERR("no OpenThread net_if; is CONFIG_NET_L2_OPENTHREAD on?");
		return -ENODEV;
	}
	if (!net_if_is_admin_up(iface)) {
		int rc = net_if_up(iface);

		if (rc != 0) {
			LOG_ERR("net_if_up = %d", rc);
			return rc;
		}
	}

	int rc = openthread_run();

	if (rc != 0) {
		LOG_ERR("openthread_run = %d", rc);
		return rc;
	}

	(void)k_thread_create(&gate_beat_thread, gate_beat_stack, GATE_BEAT_STACK_SZ, gate_beat,
			      NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	(void)k_thread_name_set(&gate_beat_thread, "gate_beat");

	LOG_INF("contention gate: Thread up on ch%u, radio rx-on; ranging is now "
		"sharing the core", GATE_CHANNEL);
	return 0;
}

/* After OpenThread's own SYS_INIT (CONFIG_OPENTHREAD_SYS_INIT_PRIORITY, 40).
 *
 * NOTE, and an earlier version of this comment had it backwards: APPLICATION
 * runs BEFORE main(), not after. z_sys_init_run_level(INIT_LEVEL_APPLICATION)
 * and the call to main() both happen inside bg_thread_main()
 * (zephyr/kernel/init.c:318 then :347), so this brings Thread up ahead of the
 * reader engine rather than behind it. Harmless for a contention gate, since
 * both are running long before a phone walks up, but the rework should start
 * Thread from main() after aliro_reader_start() if the reader ever needs to
 * own the radio first. */
SYS_INIT(thread_gate_start, APPLICATION, 90);
