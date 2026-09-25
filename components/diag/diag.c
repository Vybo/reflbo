#include "diag.h"

#include <fcntl.h>
#include <stdio.h>

#include "diag_internal.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"

#define DIAG_MAX_CMDLINE_LEN 256
#define DIAG_REPL_STACK_SIZE 4096
#define DIAG_REPL_PRIORITY   1
#define DIAG_REPL_CORE       tskNO_AFFINITY

static const char *TAG = "diag";

/*
 * A hand-built REPL instead of esp_console_new_repl_usb_serial_jtag(). That one probes the
 * terminal for escape-sequence support at start-up. With no terminal attached, nobody answers,
 * which stalls boot for about 1 s. Once a real terminal has answered, the console keeps sending
 * cursor-position queries that a script never answers, so tools/devlog.py wedges it.
 * Plain line mode ("dumb mode") gives every client, human or script, the same behaviour.
 */
static void diag_repl_task(void *arg)
{
    (void)arg;
    setvbuf(stdin, NULL, _IONBF, 0); /* stdin buffering is per task */

    for (;;) {
        char *line = linenoise(DIAG_PROMPT);
        if (line == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10)); /* input unavailable, e.g. the USB host went away */
            continue;
        }
        int ret = 0;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("unknown command: %s\n", line);
        } else if (err == ESP_OK && ret != 0) {
            printf("command returned %d\n", ret);
        } else if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) { /* INVALID_ARG: empty line */
            printf("console error: %s\n", esp_err_to_name(err));
        }
        linenoiseFree(line);
    }
}

esp_err_t diag_start(void)
{
    /* Enter sends CR; print CRLF for '\n'. Blocking stdin and stdout. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);

    usb_serial_jtag_driver_config_t usj_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&usj_config), TAG, "USB-Serial-JTAG driver install failed");
    usb_serial_jtag_vfs_use_driver();

    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    console_config.max_cmdline_length = DIAG_MAX_CMDLINE_LEN;
    ESP_RETURN_ON_ERROR(esp_console_init(&console_config), TAG, "console init failed");
    linenoiseSetDumbMode(1);
    linenoiseSetMaxLineLen(DIAG_MAX_CMDLINE_LEN);

    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help command");
    ESP_RETURN_ON_ERROR(diag_register_system_commands(), TAG, "system commands");
    ESP_RETURN_ON_ERROR(diag_register_display_commands(), TAG, "display commands");

    BaseType_t created = xTaskCreatePinnedToCore(diag_repl_task, "diag_repl", DIAG_REPL_STACK_SIZE, NULL,
                                                 DIAG_REPL_PRIORITY, NULL, DIAG_REPL_CORE);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "REPL task create failed");
    ESP_LOGI(TAG, "console ready on USB-Serial-JTAG");
    return ESP_OK;
}
