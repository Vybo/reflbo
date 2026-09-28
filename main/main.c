#include "app.h"
#include "diag.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

/*
 * The app task starts the console and loads the settings once it knows the board stays awake:
 * routine deep-sleep wakes skip both (spec §3.3).
 */
void app_main(void)
{
    diag_set_executor(app_execute);
    esp_err_t err = app_start();
    if (err != ESP_OK) {
        esp_log_level_set("*", ESP_LOG_INFO);
        ESP_LOGE(TAG, "app failed to start: %s; console only", esp_err_to_name(err));
        diag_set_executor(NULL); /* commands then run on the console task */
        err = diag_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
        }
    }
}
