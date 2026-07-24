/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host test for the reader bench console (apps/reader/main/app_shell.c) and
 * its entry point (main.c) against the sdkfake esp_console/linenoise/FreeRTOS
 * doubles. "Theatre" suite: the REPL never runs and the facade/reader calls
 * are recording fakes defined below, so passing proves command registration,
 * argument parsing, the start/stop single-owner guard, and app_main's wiring
 * order — not console I/O or radio behavior. Command handlers are invoked
 * directly via the fake registry (the printf output they produce is real).
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "linenoise/linenoise.h"

#include "app_shell.h"
#include "woz_diag.h"
#include "woz_uwb_facade.h"

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* ---- facade / reader recording doubles ----------------------------------- */
volatile int woz_uwb_diag_on;

static int s_start_calls, s_stop_calls, s_start_rc;
static const struct woz_uwb_aliro_cfg *s_last_cfg;
static bool s_have_range, s_have_trusted;
static int32_t s_range_cm = 123, s_trusted_cm = 111;

int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *cfg)
{
	s_start_calls++;
	s_last_cfg = cfg;
	return s_start_rc;
}

void woz_uwb_stop(void)
{
	s_stop_calls++;
}

bool woz_uwb_last_range_cm(int32_t *cm_out)
{
	*cm_out = s_range_cm;
	return s_have_range;
}

bool woz_uwb_trusted_range_cm(int32_t *cm_out)
{
	*cm_out = s_trusted_cm;
	return s_have_trusted;
}

static int s_prov_prints, s_trust_last_rc, s_trust_last_calls;
static int s_stepup_arms, s_stepup_statuses, s_reader_start_calls, s_reader_start_rc;

void aliro_reader_prov_print(void)
{
	s_prov_prints++;
}

int aliro_reader_trust_last(void)
{
	s_trust_last_calls++;
	return s_trust_last_rc;
}

void aliro_reader_stepup_arm(void)
{
	s_stepup_arms++;
}

void aliro_reader_stepup_status(void)
{
	s_stepup_statuses++;
}

int aliro_reader_start(void)
{
	s_reader_start_calls++;
	return s_reader_start_rc;
}

/* app_main's forever-loop: break out after a few fake vTaskDelay ticks. */
extern void app_main(void);
static jmp_buf s_main_out;

static void main_break(void)
{
	if (fake_delay_calls >= 3) {
		longjmp(s_main_out, 1);
	}
}

static int run1(esp_console_cmd_func_t fn, const char *a0)
{
	char *argv[1] = {(char *)a0};

	return fn(1, argv);
}

static int run2(esp_console_cmd_func_t fn, const char *a0, const char *a1)
{
	char *argv[2] = {(char *)a0, (char *)a1};

	return fn(2, argv);
}

static void t_lifecycle_guard(void)
{
	printf("-- responder single-owner guard --\n");

	okc("initially down", !app_responder_up());
	okc("start rc 0", app_responder_start() == 0);
	okc("facade started once with the demo cfg",
	    s_start_calls == 1 && s_last_cfg != NULL &&
	    s_last_cfg->session_id == 0x02b02fd4u && s_last_cfg->channel == 9u);
	okc("up after start", app_responder_up());
	okc("double start rejected (rc 1)", app_responder_start() == 1);
	okc("no second facade start", s_start_calls == 1);
	app_responder_stop();
	okc("stop drives facade", s_stop_calls == 1 && !app_responder_up());
	app_responder_stop();
	okc("stop while down is a no-op", s_stop_calls == 1);

	s_start_rc = -5;
	okc("failed start propagates rc", app_responder_start() == -5);
	okc("failed start stays down", !app_responder_up());
	s_start_rc = 0;
}

static void t_shell_registration(void)
{
	printf("-- shell registration --\n");

	fake_esp_reset();
	app_shell_start();
	okc("repl started", fake_repl_started == 1);
	okc("help registered", fake_help_registered == 1);
	okc("multiline off (flicker fix)", fake_linenoise_multiline == 0);
	okc("9 commands registered", fake_cmd_count == 9);

	static const char *want[] = {"status", "range", "aliro-start", "aliro-stop",
				     "aliro-prov", "aliro-trust", "aliro-stepup",
				     "uwbdiag", "clear"};
	int all = 1;

	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		all = all && fake_cmd_lookup(want[i]) != NULL;
	}
	okc("every expected command present", all);
}

static void t_commands(void)
{
	printf("-- command handlers --\n");

	/* status/range with and without a range latched. */
	s_have_range = false;
	s_have_trusted = false;
	okc("status (no range)", run1(fake_cmd_lookup("status"), "status") == 0);
	okc("range (none)", run1(fake_cmd_lookup("range"), "range") == 0);
	s_have_range = true;
	s_have_trusted = true;
	okc("status (with ranges)", run1(fake_cmd_lookup("status"), "status") == 0);
	okc("range (latched)", run1(fake_cmd_lookup("range"), "range") == 0);

	/* start/stop commands drive the guarded lifecycle. */
	s_start_calls = 0;
	s_stop_calls = 0;
	okc("aliro-start cmd", run1(fake_cmd_lookup("aliro-start"), "aliro-start") == 0 &&
	    s_start_calls == 1 && app_responder_up());
	okc("aliro-start cmd busy", run1(fake_cmd_lookup("aliro-start"), "aliro-start") == 0 &&
	    s_start_calls == 1);
	okc("aliro-stop cmd", run1(fake_cmd_lookup("aliro-stop"), "aliro-stop") == 0 &&
	    s_stop_calls == 1 && !app_responder_up());
	s_start_rc = -1;
	okc("aliro-start cmd failure", run1(fake_cmd_lookup("aliro-start"), "aliro-start") == 0 &&
	    !app_responder_up());
	s_start_rc = 0;

	/* prov / trust passthroughs, incl. every trust_last rc arm. */
	okc("aliro-prov cmd", run1(fake_cmd_lookup("aliro-prov"), "aliro-prov") == 0 &&
	    s_prov_prints == 1);
	s_trust_last_rc = 0;
	okc("aliro-trust added", run1(fake_cmd_lookup("aliro-trust"), "aliro-trust") == 0 &&
	    s_trust_last_calls == 1);
	s_trust_last_rc = 1;
	okc("aliro-trust nothing", run1(fake_cmd_lookup("aliro-trust"), "aliro-trust") == 0);
	s_trust_last_rc = -1;
	okc("aliro-trust failed", run1(fake_cmd_lookup("aliro-trust"), "aliro-trust") == 0);

	/* stepup: status vs arm (default). */
	okc("aliro-stepup status", run2(fake_cmd_lookup("aliro-stepup"), "aliro-stepup",
					"status") == 0 && s_stepup_statuses == 1);
	okc("aliro-stepup arm", run1(fake_cmd_lookup("aliro-stepup"), "aliro-stepup") == 0 &&
	    s_stepup_arms == 1);
	okc("aliro-stepup arm explicit", run2(fake_cmd_lookup("aliro-stepup"), "aliro-stepup",
					      "arm") == 0 && s_stepup_arms == 2);

	/* uwbdiag: on/off/query/usage. */
	woz_uwb_diag_on = 0;
	okc("uwbdiag on", run2(fake_cmd_lookup("uwbdiag"), "uwbdiag", "on") == 0 &&
	    woz_uwb_diag_on == 1);
	okc("uwbdiag off", run2(fake_cmd_lookup("uwbdiag"), "uwbdiag", "off") == 0 &&
	    woz_uwb_diag_on == 0);
	okc("uwbdiag query", run1(fake_cmd_lookup("uwbdiag"), "uwbdiag") == 0 &&
	    woz_uwb_diag_on == 0);
	okc("uwbdiag usage",
	    run2(fake_cmd_lookup("uwbdiag"), "uwbdiag", "banana") == 0 && woz_uwb_diag_on == 0);

	/* clear + dumb-mode color gating. */
	okc("clear cmd", run1(fake_cmd_lookup("clear"), "clear") == 0 &&
	    fake_linenoise_clears == 1);
	fake_linenoise_dumb = 1;
	okc("status in dumb mode (no colors)",
	    run1(fake_cmd_lookup("status"), "status") == 0);
	fake_linenoise_dumb = 0;
}

static void t_app_main(void)
{
	printf("-- app_main wiring --\n");

	/* Stop the responder so app_main's boot start really starts it. */
	app_responder_stop();
	fake_esp_reset();
	s_start_calls = 0;
	s_reader_start_calls = 0;
	fake_delay_calls = 0;
	s_have_range = true;
	fake_delay_hook = main_break;
	if (setjmp(s_main_out) == 0) {
		app_main();
	}
	fake_delay_hook = NULL;
	okc("app_main starts the responder", s_start_calls == 1 && app_responder_up());
	okc("app_main starts the reader", s_reader_start_calls == 1);
	okc("app_main starts the shell", fake_repl_started == 1);
	okc("range poll loop ran", fake_delay_calls >= 3);
}

int main(void)
{
	fake_freertos_reset();
	fake_esp_reset();

	t_lifecycle_guard();
	t_shell_registration();
	t_commands();
	t_app_main();

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
