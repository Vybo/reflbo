#include "app.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Never erase NVS on our own (AGENTS.md quick rule 3): settings fall back to defaults. */
        ESP_LOGE(TAG, "NVS unavailable (%s); settings use their defaults", esp_err_to_name(err));
    }

    err = app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app failed to start: %s", esp_err_to_name(err));
    }

    diag_set_executor(app_execute);
    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }
}
