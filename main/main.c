#include "esp_app_desc.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);
    ESP_LOGI(TAG, "reflbo ready");
}
