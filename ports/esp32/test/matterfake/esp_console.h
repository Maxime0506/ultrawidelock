/* matterfake esp_console.h — shadows the sdkfake one: the matter-lock shell
 * initializes commands with a .hint member the sdkfake struct lacks.
 * Registration is recorded (mfk_cmds) so the test invokes handlers directly. */
#ifndef MATTERFAKE_ESP_CONSOLE_H
#define MATTERFAKE_ESP_CONSOLE_H

#include "esp_err.h"

typedef struct esp_console_repl esp_console_repl_t;

typedef struct {
	const char *prompt;
	int task_core_id;
} esp_console_repl_config_t;
#define ESP_CONSOLE_REPL_CONFIG_DEFAULT()                                      \
	{                                                                      \
		.prompt = "> ", .task_core_id = -1                             \
	}

typedef struct {
	int dummy;
} esp_console_dev_uart_config_t;
#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT()                                  \
	{                                                                      \
		.dummy = 0                                                     \
	}

typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef struct {
	const char *command;
	const char *help;
	const char *hint;
	esp_console_cmd_func_t func;
} esp_console_cmd_t;

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev,
				    const esp_console_repl_config_t *cfg,
				    esp_console_repl_t **out);
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t esp_console_register_help_command(void);
esp_err_t esp_console_start_repl(esp_console_repl_t *repl);

#endif /* MATTERFAKE_ESP_CONSOLE_H */
