/**
 * @file main.c — nRF52840 BLE RSSI witness for inside/outside/threshold roles.
 *
 * Scans BLE advertisements during a short observation window, aggregates RSSI
 * for packets matching an ephemeral filter, and emits compact ASCII summaries
 * on UART. Does not store stable phone identifiers. Cannot command an unlock.
 *
 * UART commands (one line each, USB CDC):
 *   LEARN          — 5s lock onto loudest adv fingerprint; prints LEARN ok filt=XXXX
 *   FILT XXXX      — use that 16-bit fingerprint (hex); FILT 0 = accept all
 *   ADDR AA:BB:..  — only accept ads from that AdvA; ADDR 0 = clear
 *   HELP
 *
 * LED: solid green = app running; blink ~2 Hz = ADDR filter set.
 * On PCA10059 dongle, drives LD2 (led0) and RGB green (led1_green) — RGB is the
 * visible center LED; LD2 alone is easy to miss by the USB plug.
 *
 * Build:
 *   make witness-build WITNESS_ROLE=outside WITNESS_BOARD=nrf52840dongle/nrf52840
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(witness, LOG_LEVEL_INF);

#ifndef CONFIG_WITNESS_ROLE_OUTSIDE
#define CONFIG_WITNESS_ROLE_OUTSIDE 0
#endif
#ifndef CONFIG_WITNESS_ROLE_THRESHOLD
#define CONFIG_WITNESS_ROLE_THRESHOLD 0
#endif

#if defined(CONFIG_WITNESS_ROLE_THRESHOLD) && CONFIG_WITNESS_ROLE_THRESHOLD
static const char *const k_role_name = "threshold";
#elif defined(CONFIG_WITNESS_ROLE_OUTSIDE) && CONFIG_WITNESS_ROLE_OUTSIDE
static const char *const k_role_name = "outside";
#else
static const char *const k_role_name = "inside";
#endif

#define WITNESS_WINDOW_MS     2000
#define WITNESS_MAX_SAMPLES   64
#define WITNESS_REPORT_MAGIC  "WR1"
#define WITNESS_LEARN_MS      5000
#define WITNESS_LEARN_SLOTS   24
#define WITNESS_CMD_MAX       64

struct witness_window {
	uint32_t obs_session_id;
	uint16_t filter_hash; /* truncated adv fingerprint; not a stable ID */
	int64_t start_ms;
	int16_t rssi[WITNESS_MAX_SAMPLES];
	uint8_t n;
	bool active;
};

struct learn_slot {
	uint16_t fp;
	uint16_t count;
	int32_t rssi_sum;
	int16_t rssi_peak;
	bool used;
};

static struct witness_window g_win;
static K_MUTEX_DEFINE(g_lock);

/* Active session filter; 0 = accept all (legacy ambient mode). */
static uint16_t g_filt;
static bool g_learning;
static int64_t g_learn_deadline_ms;
static struct learn_slot g_learn[WITNESS_LEARN_SLOTS];

/* Optional AdvA filter from credential peer (Pi bridge pushes ADDR …). */
static bool g_addr_set;
static uint8_t g_addr[6]; /* MSB-first as printed AA:BB:… */

/* Status greens: LD2 (led0) and, on the dongle, RGB green (led1_green). */
static struct gpio_dt_spec g_leds[2];
static uint8_t g_led_n;

static const char *role_name(void)
{
	return k_role_name;
}

static void led_set_all(int on)
{
	for (uint8_t i = 0; i < g_led_n; i++) {
		(void)gpio_pin_set_dt(&g_leds[i], on ? 1 : 0);
	}
}

static void led_try_add(const struct gpio_dt_spec *led, const char *name)
{
	if (g_led_n >= ARRAY_SIZE(g_leds)) {
		return;
	}
	if (!gpio_is_ready_dt(led)) {
		LOG_WRN("%s not ready", name);
		return;
	}
	if (gpio_pin_configure_dt(led, GPIO_OUTPUT_ACTIVE) != 0) {
		LOG_WRN("%s configure failed", name);
		return;
	}
	g_leds[g_led_n++] = *led;
}

static void led_init(void)
{
	g_led_n = 0;
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
	{
		static const struct gpio_dt_spec led =
			GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
		led_try_add(&led, "led0");
	}
#endif
#if DT_NODE_HAS_STATUS(DT_ALIAS(led1_green), okay)
	{
		static const struct gpio_dt_spec led =
			GPIO_DT_SPEC_GET(DT_ALIAS(led1_green), gpios);
		led_try_add(&led, "led1_green");
	}
#elif DT_NODE_HAS_STATUS(DT_ALIAS(led2), okay)
	{
		static const struct gpio_dt_spec led =
			GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
		led_try_add(&led, "led2");
	}
#endif
	/* Solid on = firmware running / USB path alive. */
	led_set_all(1);
	printk("led status pins=%u\n", (unsigned)g_led_n);
}

static void led_tick(void)
{
	static int64_t last_ms;
	static bool on = true;
	int64_t now;

	if (g_led_n == 0) {
		return;
	}
	if (!g_addr_set) {
		if (!on) {
			on = true;
			led_set_all(1);
		}
		return;
	}
	now = k_uptime_get();
	if (now - last_ms < 250) {
		return;
	}
	last_ms = now;
	on = !on;
	led_set_all(on ? 1 : 0);
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

static void learn_reset(void)
{
	memset(g_learn, 0, sizeof(g_learn));
	g_learning = true;
	g_learn_deadline_ms = k_uptime_get() + WITNESS_LEARN_MS;
	printk("LEARN start ms=%d role=%s\n", WITNESS_LEARN_MS, role_name());
}

static void learn_note(uint16_t fp, int8_t rssi)
{
	int free_i = -1;

	for (int i = 0; i < WITNESS_LEARN_SLOTS; i++) {
		if (g_learn[i].used && g_learn[i].fp == fp) {
			if (g_learn[i].count < 0xFFFFu) {
				g_learn[i].count++;
			}
			g_learn[i].rssi_sum += rssi;
			if (rssi > g_learn[i].rssi_peak) {
				g_learn[i].rssi_peak = rssi;
			}
			return;
		}
		if (!g_learn[i].used && free_i < 0) {
			free_i = i;
		}
	}
	if (free_i >= 0) {
		g_learn[free_i].used = true;
		g_learn[free_i].fp = fp;
		g_learn[free_i].count = 1;
		g_learn[free_i].rssi_sum = rssi;
		g_learn[free_i].rssi_peak = rssi;
	}
}

static void learn_finish(void)
{
	int best = -1;
	int16_t best_peak = -128;
	uint16_t best_count = 0;

	g_learning = false;
	for (int i = 0; i < WITNESS_LEARN_SLOTS; i++) {
		if (!g_learn[i].used || g_learn[i].count < 3) {
			continue;
		}
		if (g_learn[i].rssi_peak > best_peak ||
		    (g_learn[i].rssi_peak == best_peak && g_learn[i].count > best_count)) {
			best = i;
			best_peak = g_learn[i].rssi_peak;
			best_count = g_learn[i].count;
		}
	}
	if (best < 0) {
		printk("LEARN fail role=%s (no fingerprint)\n", role_name());
		return;
	}
	g_filt = g_learn[best].fp;
	printk("LEARN ok filt=%04x peak=%d count=%u mean=%d role=%s\n", g_filt, best_peak,
	       (unsigned)g_learn[best].count,
	       (int)(g_learn[best].rssi_sum / (int32_t)g_learn[best].count), role_name());
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

	if (g_addr_set) {
		printk("%s role=%s obs=%u filt=%04x addr=%02X%02X%02X%02X%02X%02X "
		       "n=%u mean=%d min=%d max=%d var=%d\n",
		       WITNESS_REPORT_MAGIC, role_name(), obs, filt, g_addr[0], g_addr[1],
		       g_addr[2], g_addr[3], g_addr[4], g_addr[5], n, mean, min_v, max_v,
		       (int)var);
	} else {
		printk("%s role=%s obs=%u filt=%04x n=%u mean=%d min=%d max=%d var=%d\n",
		       WITNESS_REPORT_MAGIC, role_name(), obs, filt, n, mean, min_v, max_v,
		       (int)var);
	}
}

static bool addr_matches(const bt_addr_le_t *addr)
{
	uint8_t msb[6];

	if (!g_addr_set || addr == NULL) {
		return !g_addr_set;
	}
	/* Zephyr stores AdvA LSB-first in a.val[]; g_addr is MSB-first. */
	for (int i = 0; i < 6; i++) {
		msb[i] = addr->a.val[5 - i];
	}
	return memcmp(msb, g_addr, 6) == 0;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		    struct net_buf_simple *ad)
{
	uint16_t fp;

	ARG_UNUSED(type);

	if (ad == NULL) {
		return;
	}
	fp = adv_fingerprint(ad->data, ad->len);

	k_mutex_lock(&g_lock, K_FOREVER);
	if (g_learning) {
		learn_note(fp, rssi);
		if (k_uptime_get() >= g_learn_deadline_ms) {
			k_mutex_unlock(&g_lock);
			learn_finish();
			return;
		}
		k_mutex_unlock(&g_lock);
		return;
	}
	if (g_win.active) {
		bool fp_ok = (g_win.filter_hash == 0 || g_win.filter_hash == fp);
		bool addr_ok = addr_matches(addr);

		if (fp_ok && addr_ok) {
			if (g_win.n < WITNESS_MAX_SAMPLES) {
				g_win.rssi[g_win.n++] = rssi;
			}
		}
		if ((k_uptime_get() - g_win.start_ms) >= WITNESS_WINDOW_MS) {
			k_mutex_unlock(&g_lock);
			summarize_and_emit();
			return;
		}
	}
	k_mutex_unlock(&g_lock);
}

static void trim_inplace(char *s)
{
	size_t n;
	char *p = s;

	while (*p && isspace((unsigned char)*p)) {
		p++;
	}
	if (p != s) {
		memmove(s, p, strlen(p) + 1);
	}
	n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1])) {
		s[--n] = '\0';
	}
}

static void handle_cmd(char *line)
{
	trim_inplace(line);
	if (line[0] == '\0') {
		return;
	}
	for (char *p = line; *p; p++) {
		*p = (char)toupper((unsigned char)*p);
	}

	if (strcmp(line, "HELP") == 0) {
		printk("cmds: LEARN | FILT <hex>|0 | ADDR <aa:bb:..>|0 | HELP  role=%s "
		       "filt=%04x addr=%d\n",
		       role_name(), g_filt, g_addr_set ? 1 : 0);
		return;
	}
	if (strcmp(line, "LEARN") == 0) {
		k_mutex_lock(&g_lock, K_FOREVER);
		g_win.active = false;
		learn_reset();
		k_mutex_unlock(&g_lock);
		return;
	}
	if (strncmp(line, "FILT", 4) == 0) {
		char *arg = line + 4;

		while (*arg && isspace((unsigned char)*arg)) {
			arg++;
		}
		if (*arg == '\0') {
			printk("FILT need hex (got empty) filt=%04x\n", g_filt);
			return;
		}
		g_filt = (uint16_t)strtoul(arg, NULL, 16);
		printk("FILT ok filt=%04x role=%s\n", g_filt, role_name());
		return;
	}
	if (strncmp(line, "ADDR", 4) == 0) {
		char *arg = line + 4;
		unsigned int b[6];
		int n;

		while (*arg && isspace((unsigned char)*arg)) {
			arg++;
		}
		if (*arg == '\0' || strcmp(arg, "0") == 0 || strcmp(arg, "CLEAR") == 0) {
			g_addr_set = false;
			memset(g_addr, 0, sizeof(g_addr));
			printk("ADDR ok clear role=%s\n", role_name());
			return;
		}
		n = sscanf(arg, "%02X:%02X:%02X:%02X:%02X:%02X", &b[0], &b[1], &b[2], &b[3],
			   &b[4], &b[5]);
		if (n != 6) {
			n = sscanf(arg, "%02X%02X%02X%02X%02X%02X", &b[0], &b[1], &b[2], &b[3],
				   &b[4], &b[5]);
		}
		if (n != 6) {
			printk("ADDR need AA:BB:CC:DD:EE:FF role=%s\n", role_name());
			return;
		}
		for (int i = 0; i < 6; i++) {
			g_addr[i] = (uint8_t)b[i];
		}
		g_addr_set = true;
		printk("ADDR ok %02X:%02X:%02X:%02X:%02X:%02X role=%s\n", g_addr[0], g_addr[1],
		       g_addr[2], g_addr[3], g_addr[4], g_addr[5], role_name());
		return;
	}
	printk("unknown cmd (HELP) role=%s\n", role_name());
}

static char g_cmd_line[WITNESS_CMD_MAX];
static size_t g_cmd_len;
static struct k_msgq g_uart_msgq;
static char __aligned(4) g_uart_msgq_buf[WITNESS_CMD_MAX * 4];

static void uart_rx_feed(unsigned char c)
{
	if (c == '\r') {
		return;
	}
	if (c == '\n') {
		if (g_cmd_len > 0) {
			g_cmd_line[g_cmd_len] = '\0';
			(void)k_msgq_put(&g_uart_msgq, g_cmd_line, K_NO_WAIT);
			g_cmd_len = 0;
		}
		return;
	}
	if (g_cmd_len + 1 < sizeof(g_cmd_line)) {
		g_cmd_line[g_cmd_len++] = (char)c;
	} else {
		g_cmd_len = 0;
	}
}

static void uart_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);
	if (!uart_irq_update(dev)) {
		return;
	}
	if (!uart_irq_rx_ready(dev)) {
		return;
	}
	while (uart_fifo_read(dev, &c, 1) == 1) {
		uart_rx_feed(c);
	}
}

static int uart_cmd_init(const struct device *uart)
{
	if (uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	k_msgq_init(&g_uart_msgq, g_uart_msgq_buf, WITNESS_CMD_MAX,
		    sizeof(g_uart_msgq_buf) / WITNESS_CMD_MAX);
	/* CDC ACM only keeps accepting host OUT once IRQ RX is enabled. */
	uart_irq_callback_user_data_set(uart, uart_cb, NULL);
	uart_irq_rx_enable(uart);
	return 0;
}

static void poll_uart_cmds(const struct device *uart)
{
	char line[WITNESS_CMD_MAX];

	ARG_UNUSED(uart);
	while (k_msgq_get(&g_uart_msgq, line, K_NO_WAIT) == 0) {
		handle_cmd(line);
	}
}

int main(void)
{
	int err;
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct bt_le_scan_param scan = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	LOG_INF("ble witness role=%s", role_name());
	led_init();
	if (uart_cmd_init(uart) != 0) {
		LOG_WRN("uart cmd init failed — ADDR/FILT over USB may not work");
	}
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

	printk("witness ready role=%s filt=%04x (LEARN|FILT|ADDR|HELP)\n", role_name(), g_filt);

	for (;;) {
		static uint32_t obs;
		int64_t until;

		poll_uart_cmds(uart);
		led_tick();
		if (g_learning) {
			k_sleep(K_MSEC(20));
			continue;
		}

		obs++;
		window_reset(obs, g_filt);
		until = k_uptime_get() + WITNESS_WINDOW_MS + 200;
		while (k_uptime_get() < until) {
			poll_uart_cmds(uart);
			led_tick();
			k_sleep(K_MSEC(20));
		}
		if (g_win.active) {
			summarize_and_emit();
		}
		until = k_uptime_get() + 800;
		while (k_uptime_get() < until) {
			poll_uart_cmds(uart);
			led_tick();
			k_sleep(K_MSEC(20));
		}
	}
	return 0;
}
