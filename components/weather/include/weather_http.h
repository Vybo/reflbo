#pragma once

#include <stddef.h>

#include "esp_err.h"

/* One HTTPS GET through `fetch` (spec §11), for the sync task and the place search.
 * `buf` gets the body, NUL-terminated; a body larger than `size - 1` is ESP_ERR_INVALID_SIZE.
 * `*status` is the HTTP status (0 when no reply came). ESP_FAIL for a status other than 200. */
esp_err_t weather_http_get(const char *url, char *buf, size_t size, size_t *len, int timeout_ms, int *status);
