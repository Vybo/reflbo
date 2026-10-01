#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * HTTPS GETs with the certificate bundle and a "reflbo/<version>" User-Agent (spec §11): the sync's
 * requests, the place search, the radar's tiles and the flight radar's polls. A session keeps its
 * connection for the next GET to the same host (spec §11.2, §11.3). Device only.
 */

typedef struct {
    void *client; /* esp_http_client_handle_t; NULL until the first GET */
} fetch_session_t;

/* `url` into `buf`: the body, NUL-terminated after `*len` bytes; a body of `size - 1` bytes or more
 * is ESP_ERR_INVALID_SIZE. `*status` is the HTTP status, 0 when no reply came; ESP_FAIL for a status
 * other than 200. A connection the server closed meanwhile is opened again, once. */
esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size, size_t *len, int timeout_ms,
                    int *status);
void fetch_close(fetch_session_t *s);
