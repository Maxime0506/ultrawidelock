/* SPDX-License-Identifier: ISC */

// Native Home Assistant MQTT publisher for the ESP32 Matter lock — see ha_mqtt.h
// for the wire contract it holds with the broker.
#include <sdkconfig.h> // CONFIG_ENABLE_HA_MQTT — nothing else here pulls it in

#include "ha_mqtt.h"

#ifdef CONFIG_ENABLE_HA_MQTT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <mqtt_client.h>
#include <nvs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ha_mqtt";

/* Runtime configuration only. Nothing about a broker is compiled in, and the
 * password is never logged, so an image is safe to share and a `hamqtt show`
 * is safe to paste into a bug report. */
#define HA_NVS_NAMESPACE "ha_mqtt"
#define HA_HOST_MAX      64
#define HA_USER_MAX      64
#define HA_PASS_MAX      128
#define HA_NODE_MAX      33 /* 32 usable + NUL; the node also names entities */
#define HA_CA_MAX        4096
#define HA_DEFAULT_PORT  8883

/* NVS caps a namespace at 15 characters and an over-long one is only loud on the
 * write side; ultrawidelock_prov_nvs.c is the writeup. The keys here are written
 * inline at each call and are four characters or fewer, so the purity gate's
 * storage-name check is what holds those, not an assert. */
_Static_assert(sizeof(HA_NVS_NAMESPACE) - 1 <= NVS_NS_NAME_MAX_SIZE - 1,
	       "NVS namespace name is longer than NVS allows (NVS_NS_NAME_MAX_SIZE - 1)");

/* The Home Assistant device model, "<target> credential lock". The agent publishes
 * the nRF5340 form of the same string (ultrawidelock_ha.mqtt.DEFAULT_MODEL); the
 * shape is the contract, the target is what tells two boards apart in HA. */
#if CONFIG_IDF_TARGET_ESP32S3
#define HA_MQTT_MODEL "ESP32-S3 credential lock"
#elif CONFIG_IDF_TARGET_ESP32C5
#define HA_MQTT_MODEL "ESP32-C5 credential lock"
#elif CONFIG_IDF_TARGET_ESP32C6
#define HA_MQTT_MODEL "ESP32-C6 credential lock"
#else
#define HA_MQTT_MODEL CONFIG_IDF_TARGET " credential lock"
#endif

/* Distance rate limit. The approach controller conditions one sample
 * per ranging block, roughly every 192 ms — about 5 Hz for as long as a phone is
 * in range, which is far more than Home Assistant records and enough to make a
 * walk-up a visible burst on a shared broker. Publish at most once a second, but
 * let a change of 100 mm or more through immediately so an approach or a retreat
 * is never delayed behind the interval. */
#define HA_DISTANCE_MIN_INTERVAL_MS       1000
#define HA_DISTANCE_SIGNIFICANT_CHANGE_MM 100

/* Deep enough to ride out a TLS write stall, shallow enough that a stalled
 * broker cannot hoard RAM. Producers drop rather than wait; see the send. */
#define HA_QUEUE_DEPTH 8

enum ha_msg_kind {
	HA_MSG_DISTANCE_MM,
	HA_MSG_ACCESS,
};

// One queued observation: the smallest thing that can carry either topic's payload.
struct ha_msg {
	uint8_t kind;
	int32_t value; /* millimetres, or 1/0 for granted/denied */
};

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static esp_mqtt_client_handle_t s_client;
/* Written by the mqtt task's event callback, read by the publisher task. Plain
 * bools: each is a single-writer flag and a stale read costs at most one 200 ms
 * loop, so a lock would buy nothing. */
static volatile bool s_connected;
static volatile bool s_announced;
static volatile bool s_announce_pending;

// Broker configuration as loaded from NVS. The CA is heap-allocated because a PEM
// chain is far larger than everything else here put together.
static struct {
	char host[HA_HOST_MAX];
	char user[HA_USER_MAX];
	char pass[HA_PASS_MAX];
	char node[HA_NODE_MAX];
	char *ca;
	uint16_t port;
} s_cfg;

static char s_topic_status[HA_NODE_MAX + 16];
static char s_topic_distance[HA_NODE_MAX + 20];
static char s_topic_access[HA_NODE_MAX + 20];
static char s_topic_disc_distance[HA_NODE_MAX + 48];
static char s_topic_disc_access[HA_NODE_MAX + 48];

/* ---- configuration ------------------------------------------------------ */

// True for a node name that is safe both as an MQTT topic level and as a bare JSON
// string, so the discovery payloads below never need escaping.
static bool node_name_ok(const char *node)
{
	size_t n = strlen(node);

	if (n == 0 || n >= HA_NODE_MAX) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		char c = node[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '-')) {
			return false;
		}
	}
	return true;
}

// Store one string under key in the ha_mqtt namespace, or erase it when value is
// NULL. Returns ESP_OK on success.
static esp_err_t cfg_set_str(const char *key, const char *value)
{
	nvs_handle_t h;
	esp_err_t err = nvs_open(HA_NVS_NAMESPACE, NVS_READWRITE, &h);

	if (err != ESP_OK) {
		return err;
	}
	err = value != NULL ? nvs_set_str(h, key, value) : nvs_erase_key(h, key);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return err;
}

// Read one string from the ha_mqtt namespace into out. Returns false when the key
// is absent or does not fit.
static bool cfg_get_str(nvs_handle_t h, const char *key, char *out, size_t len)
{
	size_t used = len;

	if (nvs_get_str(h, key, out, &used) != ESP_OK) {
		out[0] = '\0';
		return false;
	}
	return out[0] != '\0';
}

// Load every configured value into s_cfg. Returns false (naming the first missing
// item) unless the broker is fully provisioned: host, login, node and pinned CA.
// Authentication is required rather than optional, matching the agent's refusal to
// connect anonymously without an explicit opt-in.
static bool cfg_load(void)
{
	nvs_handle_t h;
	size_t ca_len = 0;
	const char *missing = NULL;

	if (nvs_open(HA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
		ESP_LOGW(TAG, "no broker provisioned; run `hamqtt` at the console");
		return false;
	}
	if (!cfg_get_str(h, "host", s_cfg.host, sizeof(s_cfg.host))) {
		missing = "broker host";
	} else if (!cfg_get_str(h, "user", s_cfg.user, sizeof(s_cfg.user))) {
		missing = "username";
	} else if (!cfg_get_str(h, "pass", s_cfg.pass, sizeof(s_cfg.pass))) {
		missing = "password";
	} else if (!cfg_get_str(h, "node", s_cfg.node, sizeof(s_cfg.node))) {
		missing = "node name";
	}
	if (missing == NULL) {
		uint16_t port = HA_DEFAULT_PORT;

		nvs_get_u16(h, "port", &port);
		s_cfg.port = port;

		free(s_cfg.ca);
		s_cfg.ca = NULL;
		if (nvs_get_str(h, "ca", NULL, &ca_len) != ESP_OK || ca_len < 2 ||
		    ca_len > HA_CA_MAX) {
			missing = "CA certificate";
		} else if ((s_cfg.ca = calloc(1, ca_len)) == NULL) {
			missing = "memory for the CA certificate";
		} else if (nvs_get_str(h, "ca", s_cfg.ca, &ca_len) != ESP_OK) {
			free(s_cfg.ca);
			s_cfg.ca = NULL;
			missing = "CA certificate";
		}
	}
	nvs_close(h);

	if (missing != NULL) {
		ESP_LOGW(TAG, "broker not provisioned: no %s (run `hamqtt`)", missing);
		return false;
	}
	if (!node_name_ok(s_cfg.node)) {
		ESP_LOGE(TAG, "node name is not a valid topic level");
		return false;
	}
	return true;
}

// Build every topic once, from the node name the agent also keys its topics on.
static void build_topics(void)
{
	snprintf(s_topic_status, sizeof(s_topic_status), "ultrawidelock/%s/status", s_cfg.node);
	snprintf(s_topic_distance, sizeof(s_topic_distance), "ultrawidelock/%s/distance", s_cfg.node);
	snprintf(s_topic_access, sizeof(s_topic_access), "ultrawidelock/%s/access", s_cfg.node);
	snprintf(s_topic_disc_distance, sizeof(s_topic_disc_distance),
		 "homeassistant/sensor/%s/distance/config", s_cfg.node);
	snprintf(s_topic_disc_access, sizeof(s_topic_disc_access),
		 "homeassistant/event/%s/access/config", s_cfg.node);
}

/* ---- discovery ---------------------------------------------------------- */

/* Both payloads are field-for-field the ones discovery_payloads() emits in
 *, in the same key order, so
 * a Home Assistant install already carrying the agent's device sees no change
 * beyond the model. The node name is validated above, so no field needs JSON
 * escaping. */
#define HA_DEVICE_BLOCK_FMT                                                                        \
	"\"device\": {\"identifiers\": [\"%s\"], \"name\": \"%s\", "                               \
	"\"manufacturer\": \"ultrawidelock\", \"model\": \"" HA_MQTT_MODEL "\"}}"

/**
 * Build a Home Assistant MQTT Discovery payload for the Distance sensor entity in millimeters.
 */
static int discovery_distance_payload(char *buf, size_t len)
{
	return snprintf(buf, len,
			"{\"name\": \"Distance\", \"unique_id\": \"%s_distance\", "
			"\"state_topic\": \"ultrawidelock/%s/distance\", "
			"\"availability_topic\": \"ultrawidelock/%s/status\", "
			"\"unit_of_measurement\": \"mm\", \"device_class\": \"distance\", "
			"\"state_class\": \"measurement\", " HA_DEVICE_BLOCK_FMT,
			s_cfg.node, s_cfg.node, s_cfg.node, s_cfg.node, s_cfg.node);
}

/**
 * Build a Home Assistant MQTT Discovery payload for the Access event entity with available and
 * granted/denied event types.
 */
static int discovery_access_payload(char *buf, size_t len)
{
	return snprintf(buf, len,
			"{\"name\": \"Access\", \"unique_id\": \"%s_access\", "
			"\"state_topic\": \"ultrawidelock/%s/access\", "
			"\"availability_topic\": \"ultrawidelock/%s/status\", "
			"\"event_types\": [\"granted\", \"denied\"], " HA_DEVICE_BLOCK_FMT,
			s_cfg.node, s_cfg.node, s_cfg.node, s_cfg.node, s_cfg.node);
}

// Publish retained discovery and online availability, once per connection. Runs on
// the publisher task, never in the mqtt event callback, because
// esp_mqtt_client_publish() sends in the caller's context and would deadlock there.
static void announce(void)
{
	char payload[640];

	discovery_distance_payload(payload, sizeof(payload));
	esp_mqtt_client_publish(s_client, s_topic_disc_distance, payload, 0, 1, 1);
	discovery_access_payload(payload, sizeof(payload));
	esp_mqtt_client_publish(s_client, s_topic_disc_access, payload, 0, 1, 1);
	esp_mqtt_client_publish(s_client, s_topic_status, "online", 0, 1, 1);
	s_announced = true;
	ESP_LOGI(TAG, "announced discovery for node %s", s_cfg.node);
}

/* ---- client ------------------------------------------------------------- */

// esp-mqtt event callback. Runs on the mqtt task and only flips flags; every publish
// happens on the publisher task, which reads them.
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
	(void)arg;
	(void)base;
	(void)data;

	switch ((esp_mqtt_event_id_t)event_id) {
	case MQTT_EVENT_CONNECTED:
		s_connected = true;
		/* Re-announce on every reconnect, so a broker restart that dropped
		 * the retained topics does not strand the entities. The publisher
		 * task picks this up on its next 200 ms turn; a task blocked on the
		 * queue is not woken by a notification, so there is nothing to send
		 * it here. */
		s_announce_pending = true;
		break;
	case MQTT_EVENT_DISCONNECTED:
		s_connected = false;
		s_announced = false;
		ESP_LOGW(TAG, "broker disconnected; retrying");
		break;
	case MQTT_EVENT_ERROR:
		/* Deliberately not the broker or the credentials: this line lands in
		 * every bug report. */
		ESP_LOGW(TAG, "broker connection error");
		break;
	default:
		break;
	}
}

// Bring up the TLS client against the pinned CA. Returns ESP_OK once esp-mqtt owns
// the connection; it reconnects on its own from then on.
static esp_err_t client_start(void)
{
	esp_mqtt_client_config_t cfg = {0};

	cfg.broker.address.hostname = s_cfg.host;
	cfg.broker.address.port = s_cfg.port;
	cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
	/* Pinned CA, and the certificate must still name the host we dialled:
	 * skip_cert_common_name_check stays false, matching the agent's
	 * tls_insecure_set(False). certificate_len 0 means "NUL-terminated PEM". */
	cfg.broker.verification.certificate = s_cfg.ca;
	cfg.credentials.username = s_cfg.user;
	cfg.credentials.authentication.password = s_cfg.pass;
	cfg.session.last_will.topic = s_topic_status;
	cfg.session.last_will.msg = "offline";
	cfg.session.last_will.qos = 1;
	cfg.session.last_will.retain = 1;
	cfg.session.keepalive = 60;
	/* The 10 s default is a TLS handshake budget, and this target does the
	 * handshake in software at a priority below the walk-up. Give it room
	 * rather than buying speed with priority: a slow connect costs nothing,
	 * a connect that outranks ranging costs a missed slot deadline. */
	cfg.network.timeout_ms = 30000;
	/* Below the credential reader task (5) and far below the NimBLE host, so the
	 * TLS handshake — software P-256 on this target — is preempted by the
	 * walk-up rather than competing with it. */
	cfg.task.priority = 3;

	s_client = esp_mqtt_client_init(&cfg);
	if (s_client == NULL) {
		return ESP_FAIL;
	}
	esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
	return esp_mqtt_client_start(s_client);
}

// Publisher task: the only place that touches the broker. Everything upstream of it
// hands over a queue entry and returns immediately.
static void ha_mqtt_task(void *arg)
{
	(void)arg;

	/* Topics first: the last will is one of them, and it has to be in the
	 * CONNECT packet client_start() sends. */
	if (!cfg_load()) {
		s_task = NULL;
		vTaskDelete(NULL);
		return;
	}
	build_topics();
	if (client_start() != ESP_OK) {
		ESP_LOGE(TAG, "MQTT client failed to start");
		s_task = NULL;
		vTaskDelete(NULL);
		return;
	}
	ESP_LOGI(TAG, "publishing to %s:%u as node %s", s_cfg.host, (unsigned)s_cfg.port,
		 s_cfg.node);

	while (true) {
		struct ha_msg msg;
		char payload[32];

		if (s_announce_pending && s_connected) {
			s_announce_pending = false;
			announce();
		}
		/* 200 ms so a connect that lands while the queue is quiet still
		 * announces promptly; a queued observation wakes this immediately. */
		if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(200)) != pdTRUE) {
			continue;
		}
		if (!s_announced) {
			/* Discovery has not landed yet: Home Assistant would drop the
			 * value anyway, and the entity would be created from it. */
			continue;
		}
		if (msg.kind == HA_MSG_DISTANCE_MM) {
			snprintf(payload, sizeof(payload), "%d", (int)msg.value);
			esp_mqtt_client_publish(s_client, s_topic_distance, payload, 0, 0, 0);
		} else {
			esp_mqtt_client_publish(s_client, s_topic_access,
						msg.value ? "{\"event_type\":\"granted\"}"
							  : "{\"event_type\":\"denied\"}",
						0, 0, 0);
		}
	}
}

/* ---- producers ---------------------------------------------------------- */

// Hand one observation to the publisher task. Drops rather than blocks: the callers
// are the credential reader task and the NimBLE host task running the credential
// transaction, where the UWB responder holds a ~1.8 ms slot deadline and a stalled
// broker must never become walk-up latency. A dropped distance sample is replaced
// by the next block 192 ms later; a dropped access event is only possible behind
// eight unsent messages, which already means the broker is gone.
static void ha_mqtt_enqueue(uint8_t kind, int32_t value)
{
	struct ha_msg msg = {.kind = kind, .value = value};

	if (s_queue == NULL) {
		return;
	}
	xQueueSend(s_queue, &msg, 0);
}

void ha_mqtt_publish_distance_cm(int32_t cm)
{
	static int64_t last_published_ms;
	static int32_t last_mm;
	static bool have_last;
	int64_t now_ms = esp_timer_get_time() / 1000;
	int32_t mm;

	if (cm < 0) { /* the approach controller has no estimate yet */
		return;
	}
	mm = cm * 10;
	if (have_last && now_ms - last_published_ms < HA_DISTANCE_MIN_INTERVAL_MS &&
	    abs((int)(mm - last_mm)) < HA_DISTANCE_SIGNIFICANT_CHANGE_MM) {
		return;
	}
	last_published_ms = now_ms;
	last_mm = mm;
	have_last = true;
	ha_mqtt_enqueue(HA_MSG_DISTANCE_MM, mm);
}

void ha_mqtt_publish_access(bool granted)
{
	ha_mqtt_enqueue(HA_MSG_ACCESS, granted ? 1 : 0);
}

void ha_mqtt_start_once(void)
{
	if (s_task != NULL) {
		return;
	}
	if (s_queue == NULL) {
		s_queue = xQueueCreate(HA_QUEUE_DEPTH, sizeof(struct ha_msg));
		if (s_queue == NULL) {
			ESP_LOGE(TAG, "publish queue allocation failed");
			return;
		}
	}
	/* 6 KiB: the QoS 1 discovery publishes run mbedTLS's write path in this
	 * task's context. `status` reports nothing for it, so keep the margin. */
	xTaskCreate(ha_mqtt_task, "ha_mqtt", 6144, NULL, 3, &s_task);
}

/* ---- console ------------------------------------------------------------ */

// Read from the console without going through linenoise, so a secret typed here
// never enters the shell's in-RAM history where an up-arrow would recall it. In
// multiline mode, collect until a line containing only ".". Returns a heap string
// the caller frees, or NULL on overflow or end of input. Nothing is echoed.
static char *read_console_block(size_t max, bool multiline)
{
	char *buf = calloc(1, max);
	char line[160];
	size_t used = 0;

	if (buf == NULL) {
		return NULL;
	}
	while (fgets(line, sizeof(line), stdin) != NULL) {
		size_t n = strlen(line);

		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
			line[--n] = '\0';
		}
		if (multiline && strcmp(line, ".") == 0) {
			break;
		}
		if (used + n + 2 > max) {
			free(buf);
			return NULL;
		}
		memcpy(buf + used, line, n);
		used += n;
		if (!multiline) {
			break;
		}
		buf[used++] = '\n';
	}
	buf[used] = '\0';
	if (used == 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

// Print what is provisioned and what the publisher is doing. The password is
// reported as set or unset and never rendered.
static void cmd_show(void)
{
	nvs_handle_t h;
	char value[HA_HOST_MAX];
	uint16_t port = HA_DEFAULT_PORT;
	size_t len = 0;

	if (nvs_open(HA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
		printf("hamqtt: nothing provisioned\n");
		return;
	}
	nvs_get_u16(h, "port", &port);
	printf("broker: %s:%u\n",
	       cfg_get_str(h, "host", value, sizeof(value)) ? value : "(unset)", (unsigned)port);
	printf("user  : %s\n", cfg_get_str(h, "user", value, sizeof(value)) ? value : "(unset)");
	printf("pass  : %s\n", nvs_get_str(h, "pass", NULL, &len) == ESP_OK ? "(set)" : "(unset)");
	printf("node  : %s\n", cfg_get_str(h, "node", value, sizeof(value)) ? value : "(unset)");
	if (nvs_get_str(h, "ca", NULL, &len) == ESP_OK) {
		printf("ca    : %u bytes\n", (unsigned)len);
	} else {
		printf("ca    : (unset)\n");
	}
	printf("model : %s\n", HA_MQTT_MODEL);
	printf("state : %s\n", s_task == NULL	? "not started"
			       : s_announced	? "online"
						: "connecting");
	/* A TLS session needs a contiguous block, and this board runs Matter,
	 * NimBLE, Wi-Fi and the reader alongside it. When a connect never
	 * completes, the largest free block is the number that explains it —
	 * the total can look healthy while nothing contiguous is left. */
	printf("heap  : %u B free internal, largest block %u B\n",
	       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
	       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
	if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) != 0) {
		printf("psram : %u B free, largest block %u B\n",
		       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
		       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
	}
	nvs_close(h);
}

int ha_mqtt_shell_cmd(int argc, char **argv)
{
	esp_err_t err = ESP_OK;

	if (argc == 2 && strcmp(argv[1], "show") == 0) {
		cmd_show();
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "broker") == 0) {
		uint16_t port = argc >= 4 ? (uint16_t)atoi(argv[3]) : HA_DEFAULT_PORT;
		nvs_handle_t h;

		err = cfg_set_str("host", argv[2]);
		if (err == ESP_OK && nvs_open(HA_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
			err = nvs_set_u16(h, "port", port);
			if (err == ESP_OK) {
				err = nvs_commit(h);
			}
			nvs_close(h);
		}
		printf("hamqtt broker: %s:%u (%s)\n", argv[2], (unsigned)port,
		       esp_err_to_name(err));
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "user") == 0) {
		err = cfg_set_str("user", argv[2]);
		printf("hamqtt user: %s (%s)\n", argv[2], esp_err_to_name(err));
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "node") == 0) {
		if (!node_name_ok(argv[2])) {
			printf("hamqtt node: rejected — use 1-32 of [A-Za-z0-9_-]\n");
			return 0;
		}
		err = cfg_set_str("node", argv[2]);
		printf("hamqtt node: %s (%s)\n", argv[2], esp_err_to_name(err));
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "pass") == 0) {
		/* Taken from stdin rather than argv so it is not recallable from the
		 * shell history. Nothing echoes it, and nothing logs it. */
		char *secret;

		printf("broker password (not echoed), then enter:\n");
		secret = read_console_block(HA_PASS_MAX, false);
		if (secret == NULL) {
			printf("hamqtt pass: nothing read\n");
			return 0;
		}
		err = cfg_set_str("pass", secret);
		memset(secret, 0, strlen(secret));
		free(secret);
		printf("hamqtt pass: stored (%s)\n", esp_err_to_name(err));
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "ca") == 0) {
		char *pem;

		printf("paste the broker CA certificate in PEM (not echoed), then a "
		       "line with a single '.':\n");
		pem = read_console_block(HA_CA_MAX, true);
		if (pem == NULL) {
			printf("hamqtt ca: nothing read, or larger than %u bytes\n",
			       (unsigned)HA_CA_MAX);
			return 0;
		}
		err = cfg_set_str("ca", pem);
		printf("hamqtt ca: stored %u bytes (%s)\n", (unsigned)strlen(pem),
		       esp_err_to_name(err));
		free(pem);
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "clear") == 0) {
		nvs_handle_t h;

		if (nvs_open(HA_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
			err = nvs_erase_all(h);
			if (err == ESP_OK) {
				err = nvs_commit(h);
			}
			nvs_close(h);
		}
		printf("hamqtt clear: erased (%s); reboot to drop a live connection\n",
		       esp_err_to_name(err));
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "start") == 0) {
		ha_mqtt_start_once();
		printf("hamqtt start: %s\n", s_task != NULL ? "publisher running"
							    : "not started, see the log");
		return 0;
	}
	printf("usage: hamqtt <show|broker <host> [port]|user <name>|pass|node <name>|ca|clear|start>\n"
	       "  pass and ca read from the console, so neither reaches the shell history\n");
	return 0;
}

#endif /* CONFIG_ENABLE_HA_MQTT */
