/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 0 contention gate for putting Matter on this board.
 *
 * The question this answers is not "does Thread build" -- it does -- but whether
 * the ranging that commit 5b8d06b fought for survives sharing the M4 and the
 * 2.4 GHz radio with an 802.15.4 stack. That commit won the walk-up by cutting
 * FINAL->arm latency from 3.0 ms to 0.66 ms; OpenThread's stack processing and
 * radio interrupts land straight on that margin. If ranging degrades here, no
 * amount of Matter code above it matters, so this is measured before any is
 * written.
 *
 * Deliberately harsh, so a pass means something:
 *   - MED, not SED (CONFIG_OPENTHREAD_MTD without MTD_SED): the radio stays on
 *     continuously instead of sleeping between polls.
 *   - Channel 15 (2425 MHz) sits on top of BLE advertising channel 38
 *     (2426 MHz), which is the worst coexistence case rather than a flattering
 *     one.
 * A sleepy end device on a quiet channel is strictly easier than this.
 *
 * Never on in a shipping image: CONFIG_ALIRO_THREAD_GATE defaults n, and the
 * whole file compiles out with it.
 *
 * HISTORY, because the first version of this file measured nothing. It was
 * written against a CONFIG_NETWORKING=n "bare OpenThread" build, which links the
 * Thread stack with NO RADIO DRIVER under it --
 * zephyr/drivers/ieee802154/Kconfig gates the whole 802.15.4 driver class on
 * `depends on NETWORKING`. On hardware the image booted and this file produced
 * no output at all, because there was no radio to bring up. The config that
 * works is NET_L2_OPENTHREAD, and that is what this file now targets.
 *
 * Order matters and is not obvious. The L2 brings the interface up during net
 * init, which normally starts OpenThread immediately, before this file could
 * install a dataset. CONFIG_OPENTHREAD_MANUAL_START=y
 * (zephyr/modules/openthread/Kconfig:48) makes openthread_enable() return 0
 * without starting anything (zephyr/subsys/net/l2/openthread/openthread.c:396),
 * so the interface still comes up but the stack waits. This file then commits
 * the dataset and calls openthread_run() itself. That call finds
 * otDatasetIsCommissioned() true and skips its own credential handling
 * (zephyr/modules/openthread/openthread.c:426), which is exactly the intent:
 * the gate's dataset wins.
 *
 * The gate needs a PEER. An MTD cannot become leader, so alone on channel 15
 * this board sits DETACHED emitting parent requests forever -- honest RF
 * arbitration, but almost no stack processing, and stack processing is the half
 * that lands on the FINAL->arm margin. The peer is an nRF5340DK running
 * nrf/samples/openthread/cli as an FTD, given the same dataset by hand:
 *
 *   ot dataset clear
 *   ot dataset activetimestamp 1
 *   ot dataset channel 15
 *   ot dataset panid 0xf00d
 *   ot dataset extpanid dead00beef00cafe
 *   ot dataset networkkey 00112233445566778899aabbccddeeff
 *   ot dataset networkname openaliro-gate
 *   ot dataset commit active
 *   ot ifconfig up
 *   ot thread start
 *
 * The ext PAN ID is set on the peer but not here, because the child adopts the
 * leader's active dataset once attached; only channel, PAN ID and network key
 * have to agree up front. Load is `ot ping async <mesh-local EID> 64 <n> 0.1`,
 * which the CDK's own Zephyr stack answers, so both radio directions are
 * exercised rather than RX alone.
 *
 * Build it with the provisioning console off (reader + console + Thread
 * overflows RAM by 1,752 B) and an 8 KB RTT ring:
 *
 *   CONFIG_ALIRO_PROV_CONSOLE=n  CONFIG_SHELL=n
 *   CONFIG_USB_DEVICE_STACK=n    CONFIG_USB_CDC_ACM=n
 *   CONFIG_NETWORKING=y  CONFIG_NET_IPV6=y  CONFIG_NET_UDP=y
 *   CONFIG_NET_SOCKETS=y CONFIG_NET_L2_OPENTHREAD=y CONFIG_OPENTHREAD_MTD=y
 *   CONFIG_ALIRO_THREAD_GATE=y   CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=8192
 *
 * The RTT size is not cosmetic. At the default 4096 the ring filled partway
 * through ranging and Zephyr's NO_BLOCK_SKIP policy then silently DROPPED the
 * rest, taking the grant line with it and costing a walk-up to discover.
 * Deliberately NOT combined with the RAM trims: those are unverified on
 * hardware too, and a failed walk-up has to name one cause.
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
#define GATE_NET_NAME  "openaliro-gate"

static const uint8_t gate_network_key[OT_NETWORK_KEY_SIZE] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

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
