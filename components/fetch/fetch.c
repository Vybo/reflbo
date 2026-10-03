#include "fetch.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "fetch";

void fetch_close(fetch_session_t *s)
{
    if (s->client != NULL) {
        esp_http_client_cleanup(s->client);
        s->client = NULL;
    }
}

/* One request on the session's client, opened here if it has none. */
static esp_err_t get_once(fetch_session_t *s, const char *url, char *buf, size_t size, size_t *len, int timeout_ms,
                          int *status)
{
    *len = 0;
    *status = 0;
    esp_http_client_handle_t client = s->client;
    esp_err_t err = ESP_OK;
    if (client == NULL) {
        char agent[48];
        snprintf(agent, sizeof(agent), "reflbo/%s", esp_app_get_description()->version);
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = timeout_ms,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_agent = agent,
            .buffer_size = 2048,
            .buffer_size_tx = 1024, /* the forecast's request line is 451-454 B: 512 left no room (M6 review) */
        };
        client = esp_http_client_init(&cfg);
        ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client");
        s->client = client;
    } else {
        err = esp_http_client_set_url(client, url); /* another host closes the old connection */
        esp_http_client_set_timeout_ms(client, timeout_ms);
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, 0);
    }
    if (err == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        if (length == -ESP_ERR_HTTP_EAGAIN) {
            err = ESP_ERR_TIMEOUT; /* the reply's headers didn't come in time: "timeout", not ESP_FAIL */
        } else if (length < 0) {
            err = ESP_FAIL; /* no reply: a connection the server closed meanwhile reads as status -1 */
        } else {
            *status = esp_http_client_get_status_code(client);
            if (length >= (int64_t)size) {
                err = ESP_ERR_INVALID_SIZE;
            }
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
    if (err != ESP_OK || !esp_http_client_is_persistent_connection(client)) {
        fetch_close(s); /* a connection in an unknown state, or one the server closes (ČHMÚ's), isn't reused */
    }
    if (err == ESP_OK && *status != 200) {
        err = ESP_FAIL;
    }
    return err;
}

esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size, size_t *len, int timeout_ms,
                    int *status)
{
    bool reused = s->client != NULL;
    int64_t start_ms = esp_timer_get_time() / 1000;
    esp_err_t err = get_once(s, url, buf, size, len, timeout_ms, status);
    int left_ms = timeout_ms - (int)(esp_timer_get_time() / 1000 - start_ms); /* the retry gets what is left */
    if (err != ESP_OK && reused && *status <= 0 && left_ms >= 1000) { /* the kept connection was closed: once more */
        err = get_once(s, url, buf, size, len, left_ms, status);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %.48s...: %s, HTTP %d, %u bytes", url, esp_err_to_name(err), *status, (unsigned)*len);
    }
    return err;
}
