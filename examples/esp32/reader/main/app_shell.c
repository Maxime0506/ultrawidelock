// ESP32-IDF console shell for the standalone credential UWB responder bench app: registers status, range, ultrawidelock-start/stop, provisioning, trust, and clear commands and runs the linenoise-based REPL.
/*
 * app_shell — see app_shell.h. Interactive console + demo responder lifecycle.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"

#include <ultrawidelock/uwb.h>
#include "ultrawidelock_diag.h" /* ultrawidelock_uwb_diag_on — the raw per-frame UWB trace gate */
#include <ultrawidelock/reader.h>
#include "ultrawidelock_lab.h" /* ultrawidelock_lab_set_enabled — the [ALAB] trace runtime gate */
#include "app_shell.h"
#if defined(CONFIG_ULTRAWIDELOCK_PRESENCE)
#include "presence_link.h"
#endif

/* ---- look & feel -------------------------------------------------------- *
 * All color goes through col(): a terminal that failed the escape-sequence
 * probe (linenoise dumb mode) gets plain text instead of escape garbage. */
#define C_TITLE "\x1b[1;36m" /* bold cyan */
#define C_DIM   "\x1b[90m"   /* grey */
#define C_OK    "\x1b[32m"   /* green */
#define C_BAD   "\x1b[31m"   /* red */
#define C_RST   "\x1b[0m"

// Returns the given ANSI color code, or an empty string when linenoise is in dumb-terminal mode.
static const char *col(const char *c)
{
	return linenoiseIsDumbMode() ? "" : c;
}

// Prints the shell's startup banner: app name, version, IDF version, and a one-line usage hint.
static void print_banner(void)
{
	const esp_app_desc_t *app = esp_app_get_description();

	printf("\n%s%s%s %s%s · esp-idf %s%s\n", col(C_TITLE), app->project_name, col(C_RST),
	       col(C_DIM), app->version, esp_get_idf_version(), col(C_RST));
	printf("%scredential reader bench · 'help' lists commands · ctrl-] leaves the "
	       "monitor%s\n\n",
	       col(C_DIM), col(C_RST));
}

/* Dummy 32-byte URSK for a peerless bring-up smoke test (mirrors uwb_selftest.c).
 * Moved here from main.c so both the boot-time start and the `ultrawidelock-start`
 * command drive the exact same canned credential. */
static const uint8_t demo_ursk[32] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
	0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
};

// Demo credential UWB responder configuration used by the shell's ultrawidelock-start command.
static const struct ultrawidelock_uwb_ultrawidelock_cfg demo_cfg = {
	.session_id = 0x02b02fd4u,
	.channel = 9u,
	.sync_code_index = 9u,
	.slot_duration_rstu = 2400u,
	.block_duration_ms = 192u,
	.slot_per_round = 12u,
	.sts_index0 = 0x1196e79du,
	.uwb_time_us = 0u,
	.ursk = demo_ursk,
};

/* Single-owner guard for the DW3000 bring-up path: serializes boot + shell
 * start/stop so they can never double-start or race. s_up tracks state. */
static SemaphoreHandle_t s_lock;
static bool s_up;

// Lazily creates the s_lock mutex on first call; subsequent calls are a no-op. Not thread-safe against concurrent first calls.
static void lock_init(void)
{
	if (s_lock == NULL) {
		s_lock = xSemaphoreCreateMutex();
	}
}

int app_responder_start(void)
{
	lock_init();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int rc = 0;
	if (s_up) {
		rc = 1; /* already running */
	} else {
		rc = ultrawidelock_uwb_start_ultrawidelock(&demo_cfg);
		if (rc == 0) {
			s_up = true;
		}
	}
	xSemaphoreGive(s_lock);
	return rc;
}

void app_responder_stop(void)
{
	lock_init();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	if (s_up) {
		ultrawidelock_uwb_stop();
		s_up = false;
	}
	xSemaphoreGive(s_lock);
}

bool app_responder_up(void)
{
	return s_up;
}

/* ---- console commands -------------------------------------------------- *
 * Handlers run on the REPL task (low prio, off the radio core) and only call
 * thread-safe facade accessors or the mutex-guarded lifecycle helpers, so they
 * never touch the DW3000 bus concurrently with the responder. */

// Shell command handler: prints the demo responder's up/down status and the last measured and last trusted UWB ranges in cm, or "none" if unavailable. Always returns 0.
static int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int32_t cm;
	bool up = app_responder_up();
	printf("responder : %s%s%s\n", col(up ? C_OK : C_BAD), up ? "up" : "down", col(C_RST));
	if (ultrawidelock_uwb_last_range_cm(&cm)) {
		printf("last range: %d cm\n", (int)cm);
	} else {
		printf("last range: none\n");
	}
	if (ultrawidelock_uwb_trusted_range_cm(&cm)) {
		printf("trusted   : %d cm\n", (int)cm);
	} else {
		printf("trusted   : none\n");
	}
	return 0;
}

// Shell command handler: prints the last measured UWB range in cm via ultrawidelock_uwb_last_range_cm, or "no range yet" if none is available. Always returns 0.
static int cmd_range(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int32_t cm;
	if (ultrawidelock_uwb_last_range_cm(&cm)) {
		printf("range: %d cm\n", (int)cm);
	} else {
		printf("no range yet\n");
	}
	return 0;
}

// Shell command handler: starts the credential UWB responder via app_responder_start. Prints "busy" if a responder is already running (rc == 1), otherwise reports ok/FAILED with the return code. Always returns 0 to the shell.
static int cmd_ultrawidelock_start(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int rc = app_responder_start();
	if (rc == 1) {
		printf("busy: responder already running\n");
	} else {
		printf("ultrawidelock-start: %s (rc=%d)\n", rc == 0 ? "ok" : "FAILED", rc);
	}
	return 0;
}

// Shell command handler: stops the credential UWB responder via app_responder_stop and prints confirmation. Always returns 0.
static int cmd_ultrawidelock_stop(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	app_responder_stop();
	printf("ultrawidelock-stop: ok\n");
	return 0;
}

// Shell command handler: prints the current credential reader provisioning state. Always returns 0.
static int cmd_ultrawidelock_prov(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ultrawidelock_reader_prov_print();
	return 0;
}

// Shell command handler: toggles the raw per-frame UWB trace (cia#/PREPOLL/POLL/
// RESPTX/FINALDATA/DIST/GATE). Boot default off: the trace prints synchronously
// from the UWB task and costs ranging-slot deadlines. With no argument, prints
// the current state. Always returns 0.
static int cmd_uwbdiag(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		ultrawidelock_uwb_diag_on = 1;
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		ultrawidelock_uwb_diag_on = 0;
	} else if (argc != 1) {
		printf("usage: uwbdiag [on|off]\n");
		return 0;
	}
	printf("uwb per-frame trace: %s\n", ultrawidelock_uwb_diag_on ? "on" : "off");
	return 0;
}

// Shell command handler: toggles the [ALAB] transaction/power trace consumed by
// and (rssi, gate.hold/open/close, phase
// boundaries). Compiled in by default (CONFIG_ULTRAWIDELOCK_CRED_LAB) but off at boot so it
// costs nothing until asked for. With no argument, prints the current state.
// Always returns 0.
static int cmd_lab(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		ultrawidelock_lab_set_enabled(true);
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		ultrawidelock_lab_set_enabled(false);
	} else if (argc != 1) {
		printf("usage: lab [on|off]\n");
		return 0;
	}
	printf("ultrawidelock lab trace: %s\n", ultrawidelock_lab_enabled() ? "on" : "off");
	return 0;
}

// Shell command handler: clears the terminal screen via linenoiseClearScreen. Always returns 0.
static int cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	linenoiseClearScreen();
	return 0;
}

// Shell command handler: trusts the last-presented credential and persists it to NVS via ultrawidelock_reader_trust_last. Prints success, "nothing to add" (no credential presented or already trusted, rc == 1), or failure (trust store full or NVS error, other nonzero rc). Always returns 0 to the shell.
static int cmd_ultrawidelock_trust(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int rc = ultrawidelock_reader_trust_last();

	if (rc == 0) {
		printf("ultrawidelock-trust: added last-presented credential + saved to NVS\n");
	} else if (rc == 1) {
		printf("ultrawidelock-trust: nothing to add (no credential presented, or "
		       "already trusted)\n");
	} else {
		printf("ultrawidelock-trust: FAILED (trust store full or NVS error)\n");
	}
	return 0;
}

#if defined(CONFIG_ULTRAWIDELOCK_CRED_CLONE)
#include "ultrawidelock_prov.h" /* ULTRAWIDELOCK_PROV_BLOB_MAX */

// Maps one hex digit to its 0-15 value, or -1 if not [0-9a-fA-F].
static int hexnib(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

// Decodes an even-length hex string into out (capacity out_cap). Returns the byte
// count on success, or -1 on an odd length, a bad character, or overflow.
static int hexdecode(const char *s, uint8_t *out, size_t out_cap)
{
	size_t n = strlen(s);
	if (n == 0 || (n & 1u) || n / 2 > out_cap) {
		return -1;
	}
	for (size_t i = 0; i < n; i += 2) {
		int hi = hexnib(s[i]);
		int lo = hexnib(s[i + 1]);
		if (hi < 0 || lo < 0) {
			return -1;
		}
		out[i / 2] = (uint8_t)((hi << 4) | lo);
	}
	return (int)(n / 2);
}

// Shell command handler: serialise the reader identity + trust store (INCLUDING the
// private key) into a hex blob for cloning onto a second board. Bench only; gated by
// CONFIG_ULTRAWIDELOCK_CRED_CLONE. Always returns 0.
static int cmd_ultrawidelock_export(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	uint8_t blob[ULTRAWIDELOCK_PROV_BLOB_MAX];
	size_t len = 0;

	if (ultrawidelock_reader_export_blob(blob, sizeof(blob), &len) != 0) {
		printf("ultrawidelock-export: FAILED (buffer too small)\n");
		return 0;
	}
	printf("%sultrawidelock-export%s: %u bytes (contains the reader PRIVATE KEY -- bench only)\n",
	       col(C_BAD), col(C_RST), (unsigned)len);
	for (size_t i = 0; i < len; i++) {
		printf("%02x", blob[i]);
	}
	printf("\n");
	return 0;
}

// Shell command handler: `ultrawidelock-import <hex>`. Adopt an identity + trust store
// exported from another board via `ultrawidelock-export`, persist it, and use it live.
// Always returns 0.
static int cmd_ultrawidelock_import(int argc, char **argv)
{
	if (argc != 2) {
		printf("usage: ultrawidelock-import <hex-blob>\n");
		return 0;
	}
	uint8_t blob[ULTRAWIDELOCK_PROV_BLOB_MAX];
	int n = hexdecode(argv[1], blob, sizeof(blob));

	if (n < 0) {
		printf("ultrawidelock-import: bad hex (even length, 0-9a-f, <= %u bytes)\n",
		       (unsigned)sizeof(blob));
		return 0;
	}
	int rc = ultrawidelock_reader_import_blob(blob, (size_t)n);

	if (rc == 0) {
		printf("ultrawidelock-import: adopted %d-byte identity + trust store (saved to NVS)\n", n);
	} else if (rc == -1) {
		printf("ultrawidelock-import: malformed blob (bad magic/version/length)\n");
	} else {
		printf("ultrawidelock-import: NVS write FAILED\n");
	}
	return 0;
}
#endif /* CONFIG_ULTRAWIDELOCK_CRED_CLONE */

#if defined(CONFIG_ULTRAWIDELOCK_CRED_STEPUP)
// Shell command handler: `ultrawidelock-stepup [arm|status]`. With `arm` (or no argument)
// it arms a one-shot Access-Document request for the next transaction; `status`
// prints the armed state and the most recent verification verdict. Always 0.
static int cmd_ultrawidelock_stepup(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "status") == 0) {
		ultrawidelock_reader_stepup_status();
		return 0;
	}
	ultrawidelock_reader_stepup_arm();
	printf("ultrawidelock-stepup: armed one-shot; approach with a credential device to request "
	       "an Access Document (verdict logged, unlock unaffected)\n");
	return 0;
}
#endif

void app_shell_start(void)
{
	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	/* Defaults already give prio 2 + history_save_path = NULL (no flash writes,
	 * which would stall the instruction cache). Core 0 is the only core on C6
	 * and keeps the shell away from the IRQ worker on dual-core targets. */
	repl_cfg.prompt = "esp32>";
	repl_cfg.task_core_id = 0;
#if defined(CONFIG_ULTRAWIDELOCK_CRED_CLONE)
	/* An exported identity+trust blob is a single hex argument up to
	 * ULTRAWIDELOCK_PROV_BLOB_MAX*2 chars, past the 256-byte default line buffer. */
	repl_cfg.max_cmdline_length = 1024;
#endif

	/* UART repl on the default console UART: the prompt shares the UART0 log
	 * stream, so `make monitor`/`make term` need no change. This mirrors the
	 * nRF Zephyr shell (shell + logs interleaved on one UART). */
	esp_console_dev_uart_config_t dev_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev_cfg, &repl_cfg, &repl));

	/* esp_console defaults to multiline mode + a hints callback; either one
	 * forces linenoise to redraw prompt+line on every keystroke, which visibly
	 * flickers the cursor over the UART. With both off, typing echoes only the
	 * typed character (tab completion still works). */
	linenoiseSetMultiLine(0);
	linenoiseSetHintsCallback(NULL);

	const esp_console_cmd_t cmds[] = {
		{.command = "status",
		 .help = "responder state + last/trusted range",
		 .func = cmd_status},
		{.command = "range", .help = "print the latest distance", .func = cmd_range},
		{.command = "ultrawidelock-start",
		 .help = "start the demo DS-TWR responder",
		 .func = cmd_ultrawidelock_start},
		{.command = "ultrawidelock-stop",
		 .help = "stop the demo responder",
		 .func = cmd_ultrawidelock_stop},
		{.command = "ultrawidelock-prov",
		 .help = "show reader identity + credential trust store",
		 .func = cmd_ultrawidelock_prov},
		{.command = "ultrawidelock-trust",
		 .help = "trust the last-presented credential (persist to NVS)",
		 .func = cmd_ultrawidelock_trust},
#if defined(CONFIG_ULTRAWIDELOCK_CRED_CLONE)
		{.command = "ultrawidelock-export",
		 .help = "serialise identity+trust (incl. PRIVATE KEY) to hex for cloning",
		 .func = cmd_ultrawidelock_export},
		{.command = "ultrawidelock-import",
		 .help = "ultrawidelock-import <hex>: adopt an identity+trust blob from another board",
		 .func = cmd_ultrawidelock_import},
#endif
#if defined(CONFIG_ULTRAWIDELOCK_CRED_STEPUP)
		{.command = "ultrawidelock-stepup",
		 .help = "ultrawidelock-stepup [arm|status]: request + verify an Access Document (verdict "
			 "logged only)",
		 .func = cmd_ultrawidelock_stepup},
#endif
#if defined(CONFIG_ULTRAWIDELOCK_PRESENCE)
		{.command = "presence",
		 .help = "presence pub|credential|prove <nonce-hex>: fresh signed "
			 "post-challenge presence proof",
		 .func = presence_link_cmd},
#endif
		{.command = "uwbdiag",
		 .help = "uwbdiag [on|off]: raw per-frame UWB trace (boot default off)",
		 .func = cmd_uwbdiag},
		{.command = "lab",
		 .help = "lab [on|off]: [ALAB] transaction + power trace (boot default off)",
		 .func = cmd_lab},
		{.command = "clear", .help = "clear the screen (also: ctrl-L)", .func = cmd_clear},
	};
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
	}
	ESP_ERROR_CHECK(esp_console_register_help_command());

	/* Probe ran inside esp_console_new_repl_uart, so dumb-mode is settled and
	 * the banner lands right above the first prompt. */
	print_banner();
	ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
