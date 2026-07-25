// ESP32-IDF console shell for the Aliro Matter door lock app: registers status, range, aliro, lock/unlock, codes, factoryreset, and clear commands and runs the REPL.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * app_shell — see app_shell.h.
 */
#include <cstring>

#include <esp_console.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_idf_version.h>
#include <linenoise/linenoise.h>

#include <esp_matter.h>
#include <app/server/Server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/PlatformManager.h>
#include <setup_payload/OnboardingCodesUtil.h>

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
#include <aliro_reader.h>
#include <woz_uwb_facade.h>
#include <woz_diag.h> // woz_uwb_diag_on — the raw per-frame UWB trace gate
#ifdef CONFIG_WOZ_ALIRO_LAB
#include <aliro_lab.h>   // aliro_lab_set_enabled — the transaction-trace runtime gate
#include <uwb_cirdiag.h> // uwb_cirdiag_set_enabled — per-reception CIA diag stream, rides `lab`
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "app_shell.h"
#include "door_lock_manager.h"

using namespace chip;
using namespace chip::app::Clusters;

extern uint16_t door_lock_endpoint_id;
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
extern TaskHandle_t aliro_reader_task_handle;
#endif

/* ---- look & feel -------------------------------------------------------- *
 * All color goes through col(): a terminal that failed the escape-sequence
 * probe (linenoise dumb mode) gets plain text instead of escape garbage. */
#define C_TITLE "\x1b[1;36m" /* bold cyan */
#define C_DIM   "\x1b[90m"   /* grey */
#define C_OK    "\x1b[32m"   /* green */
#define C_BAD   "\x1b[31m"   /* red */
#define C_RST   "\x1b[0m"

// Return the ANSI color escape code c, or an empty string if linenoise is in dumb-terminal mode.
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
	printf("%sAliro Matter door lock · 'help' lists commands · ctrl-] leaves the "
	       "monitor%s\n\n",
	       col(C_DIM), col(C_RST));
}

/* ---- console commands --------------------------------------------------- *
 * Handlers run on the REPL task, off the Matter task. Anything reading CHIP
 * state takes the stack lock; anything mutating it is scheduled onto the Matter
 * task, which is the only thread allowed to drive the lock cluster. */

// Shell command handler: prints the current Matter door lock state, fabric count, and (when Aliro BLE/UWB is enabled) the last measured and last trusted UWB ranges in cm, or "none" if unavailable. Always returns 0.
static int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	app::DataModel::Nullable<DoorLock::DlLockState> lock_state;
	uint8_t fabrics;
	uint32_t feature_map = 0;

	DeviceLayer::PlatformMgr().LockChipStack();
	DoorLock::Attributes::LockState::Get(door_lock_endpoint_id, lock_state);
	DoorLock::Attributes::FeatureMap::Get(door_lock_endpoint_id, &feature_map);
	fabrics = Server::GetInstance().GetFabricTable().FabricCount();
	DeviceLayer::PlatformMgr().UnlockChipStack();

	const char *state_str = "unknown";
	bool locked = true;
	if (!lock_state.IsNull()) {
		locked = lock_state.Value() == DoorLock::DlLockState::kLocked;
		state_str = BoltLockMgr().lockStateToString(lock_state.Value());
	}
	printf("lock      : %s%s%s\n", col(locked ? C_BAD : C_OK), state_str, col(C_RST));
	printf("fabrics   : %s%u%s\n", col(fabrics ? C_OK : C_BAD), fabrics, col(C_RST));
	/* Aliro feature bits: 0x2000 AliroProvisioning, 0x4000 AliroBLEUWB. */
	printf("featuremap: 0x%04X (aliro prov %s%s%s, ble-uwb %s%s%s)\n", (unsigned)feature_map,
	       col((feature_map & 0x2000) ? C_OK : C_BAD), (feature_map & 0x2000) ? "y" : "n",
	       col(C_RST), col((feature_map & 0x4000) ? C_OK : C_BAD),
	       (feature_map & 0x4000) ? "y" : "n", col(C_RST));

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	int32_t cm;
	if (woz_uwb_last_range_cm(&cm)) {
		printf("last range: %d cm\n", (int)cm);
	} else {
		printf("last range: none\n");
	}
	if (woz_uwb_trusted_range_cm(&cm)) {
		printf("trusted   : %d cm\n", (int)cm);
	} else {
		printf("trusted   : none\n");
	}
	/* Smallest free stack ever seen, in bytes. A value near zero on any of these is
	 * the overflow to chase; the end-of-stack watchpoint will name it if it trips. */
	if (aliro_reader_task_handle != nullptr) {
		unsigned free_b =
			(unsigned)uxTaskGetStackHighWaterMark(aliro_reader_task_handle) *
			sizeof(StackType_t);
		printf("stack rdr : %s%u B free%s\n", col(free_b < 1024 ? C_BAD : C_OK), free_b,
		       col(C_RST));
	}
#endif
	return 0;
}

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
// Shell handler for the "range" command; prints the last measured UWB range in cm, or "no range yet"
// if none has been recorded. Always returns 0.
static int cmd_range(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int32_t cm;
	if (woz_uwb_last_range_cm(&cm)) {
		printf("range: %d cm\n", (int)cm);
	} else {
		printf("no range yet\n");
	}
	return 0;
}

// Shell handler for the "aliro" command. Subcommands: "prov" prints reader provisioning info;
// "trust" adds the last-presented credential to the trust store and persists it to NVS, reporting
// whether a credential was actually available to trust or whether the store/NVS write failed.
// Any other or missing argument prints usage. Always returns 0.
static int cmd_aliro(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "prov") == 0) {
		aliro_reader_prov_print();
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "trust") == 0) {
		int rc = aliro_reader_trust_last();
		if (rc == 0) {
			printf("aliro trust: added last-presented credential + saved to NVS\n");
		} else if (rc == 1) {
			printf("aliro trust: nothing to add (no credential presented, or "
			       "already trusted)\n");
		} else {
			printf("aliro trust: FAILED (trust store full or NVS error)\n");
		}
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "clear") == 0) {
		int rc = aliro_reader_trust_clear();
		if (rc == 0) {
			printf("aliro clear: trust store emptied + saved to NVS\n");
		} else if (rc == 1) {
			printf("aliro clear: already empty\n");
		} else {
			printf("aliro clear: FAILED (NVS error)\n");
		}
		return 0;
	}
	printf("usage: aliro <prov|trust|clear>\n");
	return 0;
}

// Shell handler for "uwbdiag": toggles the raw per-frame UWB trace (cia#/PREPOLL/
// POLL/RESPTX/FINALDATA/DIST/GATE). Boot default off: the trace prints
// synchronously from the UWB task and costs ranging-slot deadlines, so turn it
// on only to debug the radio path. With no argument, prints the current state.
static int cmd_uwbdiag(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		woz_uwb_diag_on = 1;
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		woz_uwb_diag_on = 0;
	} else if (argc != 1) {
		printf("usage: uwbdiag [on|off]\n");
		return 0;
	}
	printf("uwb per-frame trace: %s\n", woz_uwb_diag_on ? "on" : "off");
	return 0;
}
#endif /* CONFIG_ENABLE_ALIRO_BLE_UWB */

/* Both bolt commands hop to the Matter task: BoltLockMgr drives cluster
 * attributes + emits events, which is only safe there. */
static int cmd_lock(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
		BoltLockMgr().Lock(door_lock_endpoint_id, DoorLock::OperationSourceEnum::kManual);
	});
	printf("lock: requested\n");
	return 0;
}

// Shell handler for the "unlock" command; schedules a manual bolt unlock on the Matter work queue
// and confirms the request was submitted. Always returns 0.
static int cmd_unlock(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
		BoltLockMgr().Unlock(door_lock_endpoint_id, DoorLock::OperationSourceEnum::kManual);
	});
	printf("unlock: requested\n");
	return 0;
}

/* The boot log scrolls away long before you need to pair; this puts the QR URL
 * and manual code back on demand. */
static int cmd_codes(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	DeviceLayer::PlatformMgr().LockChipStack();
	PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));
	DeviceLayer::PlatformMgr().UnlockChipStack();
	return 0;
}

// Shell handler for the "factoryreset" command; erases persisted config and reboots the device via
// esp_matter::factory_reset(). Always returns 0 (the reboot happens before returning is meaningful).
static int cmd_factoryreset(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("factory reset: erasing and rebooting\n");
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	/* esp_matter::factory_reset() erases only Matter's own NVS namespaces; the
	 * Aliro reader identity + trust store live in "aliro_prov" and would
	 * survive, so the old home's phones could still authenticate after the
	 * reset. Revert to the dev identity (RAM + NVS) before rebooting. */
	aliro_reader_provision_clear();
#endif
	esp_matter::factory_reset();
	return 0;
}

/* Runtime log knob: the boot default is WARN (blocking UART writes in the
 * protocol callbacks cost walk-up latency), so bench diagnostics need a way
 * back up without a reflash. The compile-time ceiling is DEBUG
 * (CONFIG_LOG_MAXIMUM_LEVEL); note the shared woz_aliro/woz_uwb sources log
 * under their module tags (aliro_reader, aliro_ranging, ...). */
static int cmd_log(int argc, char **argv)
{
	static const struct {
		const char *name;
		esp_log_level_t level;
	} levels[] = {
		{"none", ESP_LOG_NONE},   {"error", ESP_LOG_ERROR}, {"warn", ESP_LOG_WARN},
		{"info", ESP_LOG_INFO},   {"debug", ESP_LOG_DEBUG}, {"verbose", ESP_LOG_VERBOSE},
	};

	if (argc == 3) {
		for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
			if (strcmp(argv[2], levels[i].name) == 0) {
				esp_log_level_set(argv[1], levels[i].level);
				printf("log: %s -> %s\n", argv[1], levels[i].name);
				return 0;
			}
		}
	}
	printf("usage: log <tag|*> <none|error|warn|info|debug|verbose>\n"
	       "boot default warn; compile-time ceiling debug\n"
	       "note: chip[..] progress/detail logs are compiled out; only chip errors\n"
	       "remain, and they respond to * only (rebuild with\n"
	       "CONFIG_LOG_DEFAULT_LEVEL_INFO=y for full chip diagnostics)\n");
	return 0;
}

#ifdef CONFIG_WOZ_ALIRO_LAB
/* Aliro Lab transaction trace: OFF at boot (the [ALAB] lines are blocking UART
 * writes on the protocol path, so they cost walk-up latency while on). `lab on`
 * before a walk-up, `lab off` after; tools/aliro_lab.py scores the captured log.
 * `lab cir on|off` additionally arms the windowed-CIR tap dump (channel-impulse
 * Stage 1): the taps buffer to RAM while armed and print in a burst on `lab cir
 * off`, off the ranging path, so the walk-up still unlocks while capturing. */
static int cmd_lab(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "cir") == 0) {
		if (strcmp(argv[2], "probe") == 0) {
			uwb_cirdiag_probe();
			return 0;
		}
		if (strcmp(argv[2], "on") == 0) {
			uwb_cirdiag_dump_set_enabled(true);
		} else if (strcmp(argv[2], "off") == 0) {
			uwb_cirdiag_dump_set_enabled(false);
		} else {
			printf("usage: lab cir [on|off|probe]\n");
			return 0;
		}
		bool cir_on = uwb_cirdiag_dump_enabled();

		printf("aliro lab CIR dump: %s%s\n", cir_on ? "on" : "off",
		       cir_on ? "  (taps print on: lab cir off)" : "");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		aliro_lab_set_enabled(true);
		uwb_cirdiag_set_enabled(true); /* per-reception ev=uwb.diag lines ride the gate */
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		aliro_lab_set_enabled(false);
		uwb_cirdiag_set_enabled(false);
	} else if (argc != 1) {
		printf("usage: lab [on|off] | lab cir [on|off|probe]\n");
		return 0;
	}
	printf("aliro lab trace: %s\n", aliro_lab_enabled() ? "on" : "off");
	return 0;
}
#endif

// Shell handler for the "clear" command; clears the terminal screen. Always returns 0.
static int cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	linenoiseClearScreen();
	return 0;
}

void app_shell_start(void)
{
	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	/* Defaults give prio 2 + history_save_path = NULL (no flash writes, which
	 * would stall both cores' cache). Pin off the Matter/radio core. */
	repl_cfg.prompt = "matter> ";
	repl_cfg.task_core_id = 0;

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
		 .help = "lock state, fabric count, last/trusted range",
		 .hint = NULL,
		 .func = cmd_status},
		{.command = "lock",
		 .help = "drive the bolt to Locked",
		 .hint = NULL,
		 .func = cmd_lock},
		{.command = "unlock",
		 .help = "drive the bolt to Unlocked",
		 .hint = NULL,
		 .func = cmd_unlock},
		{.command = "codes",
		 .help = "reprint the commissioning QR URL + manual pairing code",
		 .hint = NULL,
		 .func = cmd_codes},
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
		{.command = "range",
		 .help = "print the latest distance",
		 .hint = NULL,
		 .func = cmd_range},
		{.command = "aliro",
		 .help = "aliro <prov|trust|clear>: show identity / trust last credential / "
			 "empty trust store",
		 .hint = NULL,
		 .func = cmd_aliro},
		{.command = "uwbdiag",
		 .help = "uwbdiag [on|off]: raw per-frame UWB trace (boot default off; "
			 "costs slot deadlines)",
		 .hint = NULL,
		 .func = cmd_uwbdiag},
#ifdef CONFIG_WOZ_ALIRO_LAB
		{.command = "lab",
		 .help = "lab [on|off]: Aliro Lab transaction trace (boot default off)",
		 .hint = NULL,
		 .func = cmd_lab},
#endif
#endif
		{.command = "log",
		 .help = "log <tag|*> <level>: runtime log level (boot default warn)",
		 .hint = NULL,
		 .func = cmd_log},
		{.command = "factoryreset",
		 .help = "erase all Matter state and reboot",
		 .hint = NULL,
		 .func = cmd_factoryreset},
		{.command = "clear",
		 .help = "clear the screen (also: ctrl-L)",
		 .hint = NULL,
		 .func = cmd_clear},
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
