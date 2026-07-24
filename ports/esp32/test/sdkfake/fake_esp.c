/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * fake_esp — recording doubles for the esp_console / linenoise / app-desc /
 * log-level surface the bench-console sources use on the host.
 */
#include <string.h>

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "linenoise/linenoise.h"

esp_console_cmd_t fake_cmds[FAKE_CMD_MAX];
int fake_cmd_count;
int fake_help_registered;
int fake_repl_started;
esp_err_t fake_console_new_repl_rc = ESP_OK;

int fake_linenoise_dumb;
int fake_linenoise_clears;
int fake_linenoise_multiline = -1;

static struct esp_console_repl {
	int dummy;
} s_repl;

static const esp_app_desc_t s_app = {.project_name = "aliro-bench", .version = "test"};

void fake_esp_reset(void)
{
	memset(fake_cmds, 0, sizeof(fake_cmds));
	fake_cmd_count = 0;
	fake_help_registered = 0;
	fake_repl_started = 0;
	fake_console_new_repl_rc = ESP_OK;
	fake_linenoise_dumb = 0;
	fake_linenoise_clears = 0;
	fake_linenoise_multiline = -1;
}

void esp_log_level_set(const char *tag, esp_log_level_t level)
{
	(void)tag;
	(void)level;
}

const esp_app_desc_t *esp_app_get_description(void)
{
	return &s_app;
}

const char *esp_get_idf_version(void)
{
	return "fake-idf";
}

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev,
				    const esp_console_repl_config_t *cfg,
				    esp_console_repl_t **out)
{
	(void)dev;
	(void)cfg;
	*out = &s_repl;
	return fake_console_new_repl_rc;
}

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd)
{
	if (fake_cmd_count >= FAKE_CMD_MAX) {
		return ESP_FAIL;
	}
	fake_cmds[fake_cmd_count++] = *cmd;
	return ESP_OK;
}

esp_err_t esp_console_register_help_command(void)
{
	fake_help_registered = 1;
	return ESP_OK;
}

esp_err_t esp_console_start_repl(esp_console_repl_t *repl)
{
	(void)repl;
	fake_repl_started = 1;
	return ESP_OK;
}

esp_console_cmd_func_t fake_cmd_lookup(const char *name)
{
	for (int i = 0; i < fake_cmd_count; i++) {
		if (strcmp(fake_cmds[i].command, name) == 0) {
			return fake_cmds[i].func;
		}
	}
	return NULL;
}

int linenoiseIsDumbMode(void)
{
	return fake_linenoise_dumb;
}

void linenoiseClearScreen(void)
{
	fake_linenoise_clears++;
}

void linenoiseSetMultiLine(int ml)
{
	fake_linenoise_multiline = ml;
}

void linenoiseSetHintsCallback(linenoiseHintsCallback *fn)
{
	(void)fn;
}
