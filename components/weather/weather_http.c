#include "weather_http.h"

#include "fetch.h"

esp_err_t weather_http_get(const char *url, char *buf, size_t size, size_t *len, int timeout_ms, int *status)
{
    fetch_session_t s = { 0 };
    esp_err_t err = fetch_get(&s, url, buf, size, len, timeout_ms, status);
    fetch_close(&s);
    return err;
}
