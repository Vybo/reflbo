#include "fetch.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "util_url.h"

static const char *TAG = "fetch";

void fetch_close(fetch_session_t *s)
{
    if (s->client != NULL) {
        esp_http_client_cleanup(s->client);
        s->client = NULL;
    }
}

/* "Authorization: Bearer <bearer>", or none; a token may run to a kilobyte (SolaX's), so the value is built on the
 * heap: the client keeps its own copy. */
static esp_err_t authorize(esp_http_client_handle_t client, const char *bearer)
{
    if (bearer == NULL) {
        esp_http_client_delete_header(client, "Authorization");
        return ESP_OK;
    }
    size_t n = strlen(bearer) + sizeof("Bearer ");
    char *auth = malloc(n);
    ESP_RETURN_ON_FALSE(auth != NULL, ESP_ERR_NO_MEM, TAG, "auth");
    snprintf(auth, n, "Bearer %s", bearer);
    esp_err_t err = esp_http_client_set_header(client, "Authorization", auth);
    free(auth);
    return err;
}

/* One request on the session's client, opened here if it has none: a GET, or with `body` a POST. */
static esp_err_t request_once(fetch_session_t *s, const char *url, const char *content_type, const char *body,
                              char *buf, size_t size, size_t *len, int timeout_ms, int *status)
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
            .buffer_size_tx = 2048, /* the request line and its headers, a token's included (gotcha 41) */
        };
        client = esp_http_client_init(&cfg);
        ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client");
        s->client = client;
    } else {
        err = esp_http_client_set_url(client, url); /* another host closes the old connection */
        esp_http_client_set_timeout_ms(client, timeout_ms);
    }
    if (err == ESP_OK) {
        err = authorize(client, s->bearer);
    }
    size_t body_len = body != NULL ? strlen(body) : 0;
    if (err == ESP_OK) {
        err = esp_http_client_set_method(client, body != NULL ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    }
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_header(client, "Content-Type", content_type);
    } else if (err == ESP_OK) {
        esp_http_client_delete_header(client, "Content-Type");
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, (int)body_len);
    }
    if (err == ESP_OK && body_len > 0 && esp_http_client_write(client, body, (int)body_len) != (int)body_len) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        if (length < 0) {
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

static esp_err_t request(fetch_session_t *s, const char *url, const char *content_type, const char *body, void *buf,
                         size_t size, size_t *len, int timeout_ms, int *status)
{
    bool reused = s->client != NULL;
    esp_err_t err = request_once(s, url, content_type, body, buf, size, len, timeout_ms, status);
    if (err != ESP_OK && reused && *status <= 0) { /* the server closed the kept connection: once more */
        err = request_once(s, url, content_type, body, buf, size, len, timeout_ms, status);
    }
    if (err != ESP_OK) {
        char host[64];
        util_url_host(url, host, sizeof(host)); /* never a key or a token */
        ESP_LOGW(TAG, "%s %s: %s, HTTP %d, %u bytes", body != NULL ? "POST" : "GET", host, esp_err_to_name(err),
                 *status, (unsigned)*len);
    }
    return err;
}

esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size, size_t *len, int timeout_ms,
                    int *status)
{
    return request(s, url, NULL, NULL, buf, size, len, timeout_ms, status);
}

esp_err_t fetch_post(fetch_session_t *s, const char *url, const char *content_type, const char *body, void *buf,
                     size_t size, size_t *len, int timeout_ms, int *status)
{
    return request(s, url, content_type, body, buf, size, len, timeout_ms, status);
}
