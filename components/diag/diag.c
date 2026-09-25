#include "diag.h"

#include "diag_internal.h"
#include "esp_check.h"
#include "esp_console.h"

static const char *TAG = "diag";

esp_err_t diag_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = DIAG_PROMPT;
    repl_config.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl),
                        TAG, "console REPL init failed");
    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help command");
    ESP_RETURN_ON_ERROR(diag_register_system_commands(), TAG, "system commands");
    ESP_RETURN_ON_ERROR(esp_console_start_repl(repl), TAG, "console REPL start failed");
    ESP_LOGI(TAG, "console ready on USB-Serial-JTAG");
    return ESP_OK;
}
