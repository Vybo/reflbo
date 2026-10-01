#include "weather_http.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "weather_http";

esp_err_t weather_http_get(const char *url, char *buf, size_t size, size_t *len, int timeout_ms, int *status)
{
    *len = 0;
    *status = 0;
    char agent[48];
    snprintf(agent, sizeof(agent), "reflbo/%s", esp_app_get_description()->version);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = agent,
        .buffer_size = 2048,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client");
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        *status = esp_http_client_get_status_code(client);
        if (length >= (int64_t)size) {
            err = ESP_ERR_INVALID_SIZE;
        }
        while (err == ESP_OK) {
            if (*len + 1 >= size) {
                err = ESP_ERR_INVALID_SIZE; /* chunked and too long */
                break;
            }
            int n = esp_http_client_read(client, buf + *len, (int)(size - 1 - *len));
            if (n < 0) {
                err = ESP_FAIL;
            } else if (n == 0) {
                break; /* the end, or the timeout: is_complete tells them apart */
            } else {
                *len += (size_t)n;
            }
        }
        if (err == ESP_OK && !esp_http_client_is_complete_data_received(client)) {
            err = ESP_ERR_TIMEOUT;
        }
    }
    buf[*len] = '\0';
    esp_http_client_cleanup(client);
    if (err == ESP_OK && *status != 200) {
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %.48s...: %s, HTTP %d, %u bytes", url, esp_err_to_name(err), *status, (unsigned)*len);
    }
    return err;
}
