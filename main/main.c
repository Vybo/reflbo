#include "diag.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gfx_test_pattern.h"

static const char *TAG = "main";

/* Init sequence until the owner picks one (M1 Task 9). */
#define DISPLAY_VARIANT ST7305_VARIANT_XIAOZHI

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = display_init(DISPLAY_VARIANT);
    if (err == ESP_OK) {
        gfx_draw_test_pattern(display_fb());
        err = display_commit(false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }

    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
