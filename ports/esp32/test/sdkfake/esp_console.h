/* sdkfake esp_console.h — command registration is recorded so a test can look
 * up and invoke the handlers directly (fake_esp.c). */
#ifndef SDKFAKE_ESP_CONSOLE_H
#define SDKFAKE_ESP_CONSOLE_H

#include "esp_err.h"

typedef struct esp_console_repl esp_console_repl_t;

typedef struct {
	const char *prompt;
	int task_core_id;
} esp_console_repl_config_t;
#define ESP_CONSOLE_REPL_CONFIG_DEFAULT() {.prompt = "> ", .task_core_id = -1}

typedef struct {
	int dummy;
} esp_console_dev_uart_config_t;
#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT() {.dummy = 0}

typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef struct {
	const char *command;
	const char *help;
	esp_console_cmd_func_t func;
} esp_console_cmd_t;

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev,
				    const esp_console_repl_config_t *cfg,
				    esp_console_repl_t **out);
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t esp_console_register_help_command(void);
esp_err_t esp_console_start_repl(esp_console_repl_t *repl);

/* ---- fake_esp.c control surface ---- */
#define FAKE_CMD_MAX 16
extern esp_console_cmd_t fake_cmds[FAKE_CMD_MAX];
extern int fake_cmd_count;
extern int fake_help_registered;
extern int fake_repl_started;
extern esp_err_t fake_console_new_repl_rc;
esp_console_cmd_func_t fake_cmd_lookup(const char *name);
void fake_esp_reset(void);

#endif
