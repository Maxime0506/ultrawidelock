/**
 * @file main.c — nRF52840 BLE RSSI witness for inside/outside/threshold roles.
 *
 * Scans BLE advertisements during a short observation window, aggregates RSSI
 * for packets matching an ephemeral filter from the lock, and emits compact
 * ASCII summaries on UART. Does not store stable phone identifiers. Cannot
 * command an unlock.
 *
 * Build:
 *   make witness-build ROLE=outside BOARD=nrf52840dk/nrf52840
 * Roles: inside | outside | threshold
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(witness, LOG_LEVEL_INF);

#ifndef CONFIG_WITNESS_ROLE_OUTSIDE
#define CONFIG_WITNESS_ROLE_OUTSIDE 0
#endif
#ifndef CONFIG_WITNESS_ROLE_THRESHOLD
#define CONFIG_WITNESS_ROLE_THRESHOLD 0
#endif

enum witness_role {
	WITNESS_ROLE_INSIDE = 0,
	WITNESS_ROLE_OUTSIDE = 1,
	WITNESS_ROLE_THRESHOLD = 2,
};

#if defined(CONFIG_WITNESS_ROLE_THRESHOLD) && CONFIG_WITNESS_ROLE_THRESHOLD
static const enum witness_role k_role = WITNESS_ROLE_THRESHOLD;
static const char *const k_role_name = "threshold";
#elif defined(CONFIG_WITNESS_ROLE_OUTSIDE) && CONFIG_WITNESS_ROLE_OUTSIDE
static const enum witness_role k_role = WITNESS_ROLE_OUTSIDE;
static const char *const k_role_name = "outside";
#else
static const enum witness_role k_role = WITNESS_ROLE_INSIDE;
static const char *const k_role_name = "inside";
#endif

#define WITNESS_WINDOW_MS     2000
#define WITNESS_MAX_SAMPLES   64
#define WITNESS_REPORT_MAGIC  "WR1"

struct witness_window {
	uint32_t obs_session_id;
	uint16_t filter_hash; /* truncated adv fingerprint; not a stable ID */
	int64_t start_ms;
	int16_t rssi[WITNESS_MAX_SAMPLES];
	uint8_t n;
	bool active;
};

static struct witness_window g_win;
static K_MUTEX_DEFINE(g_lock);

static const char *role_name(void)
{
	return k_role_name;
}

static void window_reset(uint32_t obs_id, uint16_t filter_hash)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	memset(&g_win, 0, sizeof(g_win));
	g_win.obs_session_id = obs_id;
	g_win.filter_hash = filter_hash;
	g_win.start_ms = k_uptime_get();
	g_win.active = true;
	k_mutex_unlock(&g_lock);
	LOG_INF("window start obs=%u filter=%04x role=%s", obs_id, filter_hash, role_name());
}

static uint16_t adv_fingerprint(const uint8_t *data, uint8_t len)
{
	uint16_t h = 0xA5A5u;

	if (data == NULL) {
		return 0;
	}
	for (uint8_t i = 0; i < len; i++) {
		h = (uint16_t)((h * 33u) ^ data[i]);
	}
	return h;
}

static void summarize_and_emit(void)
{
	int32_t sum = 0;
	int16_t min_v = 127;
	int16_t max_v = -128;
	uint8_t n;
	uint32_t obs;
	uint16_t filt;
	int16_t mean = 0;
	int32_t var = 0;

	k_mutex_lock(&g_lock, K_FOREVER);
	n = g_win.n;
	obs = g_win.obs_session_id;
	filt = g_win.filter_hash;
	for (uint8_t i = 0; i < n; i++) {
		int16_t v = g_win.rssi[i];

		sum += v;
		if (v < min_v) {
			min_v = v;
		}
		if (v > max_v) {
			max_v = v;
		}
	}
	if (n > 0) {
		mean = (int16_t)(sum / n);
		for (uint8_t i = 0; i < n; i++) {
			int32_t d = (int32_t)g_win.rssi[i] - mean;

			var += d * d;
		}
		if (n > 1) {
			var /= (n - 1);
		}
	}
	g_win.active = false;
	k_mutex_unlock(&g_lock);

	/* Compact summary only — no BD_ADDR, no raw payload. */
	printk("%s role=%s obs=%u filt=%04x n=%u mean=%d min=%d max=%d var=%d\n",
	       WITNESS_REPORT_MAGIC, role_name(), obs, filt, n, mean, min_v, max_v, (int)var);
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		    struct net_buf_simple *ad)
{
	uint16_t fp;
	bool take = false;

	ARG_UNUSED(addr);
	ARG_UNUSED(type);

	if (ad == NULL) {
		return;
	}
	fp = adv_fingerprint(ad->data, ad->len);

	k_mutex_lock(&g_lock, K_FOREVER);
	if (g_win.active) {
		if (g_win.filter_hash == 0 || g_win.filter_hash == fp) {
			if (g_win.n < WITNESS_MAX_SAMPLES) {
				g_win.rssi[g_win.n++] = rssi;
			}
			take = true;
		}
		if ((k_uptime_get() - g_win.start_ms) >= WITNESS_WINDOW_MS) {
			k_mutex_unlock(&g_lock);
			summarize_and_emit();
			return;
		}
	}
	k_mutex_unlock(&g_lock);

	if (take) {
		/* Keep ISR/callback path quiet. */
	}
}

static void shell_start_window(uint32_t obs, uint16_t filt)
{
	window_reset(obs, filt);
}

int main(void)
{
	int err;
	struct bt_le_scan_param scan = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	LOG_INF("ble witness role=%s", role_name());
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable %d", err);
		return err;
	}
	err = bt_le_scan_start(&scan, scan_cb);
	if (err) {
		LOG_ERR("scan_start %d", err);
		return err;
	}

	/* Development loop: start a window every few seconds so UART capture
	 * works before Matter control-plane wiring lands. Production starts
	 * windows only on obs-session triggers from the primary lock. */
	for (;;) {
		static uint32_t obs;

		obs++;
		shell_start_window(obs, 0);
		k_sleep(K_MSEC(WITNESS_WINDOW_MS + 200));
		if (g_win.active) {
			summarize_and_emit();
		}
		k_sleep(K_MSEC(800));
	}
	return 0;
}
