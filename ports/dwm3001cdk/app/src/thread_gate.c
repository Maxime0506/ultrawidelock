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
 * STATUS: this code has never successfully started Thread. It was written
 * against a CONFIG_NETWORKING=n "bare OpenThread" build, which turned out to
 * link the Thread stack with NO RADIO DRIVER under it --
 * zephyr/drivers/ieee802154/Kconfig gates the whole 802.15.4 driver class on
 * `depends on NETWORKING`. On hardware the image booted and this file produced
 * no output at all, because there was no radio to bring up.
 *
 * The working Thread config is NET_L2_OPENTHREAD, where the L2 owns interface
 * bring-up, so openthread_run() below is the wrong entry point. The rework is
 * CONFIG_OPENTHREAD_MANUAL_START=y (zephyr/modules/openthread/Kconfig:48,
 * honoured by the L2 at zephyr/subsys/net/l2/openthread/openthread.c:396) so the
 * dataset can be set before start, then net_if_up().
 *
 * SECOND KNOWN FLAW, unfixed: an MTD cannot become leader, so with nothing else
 * on channel 15 this board sits DETACHED emitting parent requests forever. That
 * still gives honest RF arbitration, but almost no stack processing -- and stack
 * processing is the half that lands on the FINAL->arm margin. A pass measured
 * alone on the channel would mean far less than it looks like. The gate needs a
 * second 802.15.4 node holding the network up; the nRF5340DK already in this
 * project can do it as an ot-cli FTD.
 *
 * Left in tree because the measurement it exists to make is still the go/no-go
 * for Matter on this board.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <openthread.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
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
}

static struct openthread_state_changed_callback gate_cb = {
	.otCallback = on_state_changed,
};

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

	int rc = openthread_run();

	if (rc != 0) {
		LOG_ERR("openthread_run = %d", rc);
		return rc;
	}

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
