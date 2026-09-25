#include "diag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
