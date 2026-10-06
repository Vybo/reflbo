#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * The web configurator (spec §10.3, §10.4): an HTTP server on port 80 while config mode runs.
 *
 * - webui serves the embedded pages, redirects captive-portal probes, keeps the password and
 *   the sessions, streams OTA uploads, and answers the Wi-Fi requests through netmgr.
 * - Every other /api request goes to the app's `api` function, which runs on the app task
 *   (through `run`), so the app's state keeps one owner (spec §3.2).
 */

typedef struct {
    int status;       /* HTTP status: 200, 400, 404, 409, ... */
    const char *type; /* "application/json" or "image/bmp" */
    size_t len;       /* bytes in the out buffer */
} webui_reply_t;

/* One API request, answered into `out`. `body` is NUL-terminated, "" when there is none. */
typedef void (*webui_api_fn)(const char *method, const char *path, const char *query, const char *body,
                             uint8_t *out, size_t out_size, webui_reply_t *reply);

typedef enum {
    WEBUI_EVENT_DONE,    /* "Done" in the web UI: leave config mode */
    WEBUI_EVENT_REBOOT,  /* the Status page asked */
    WEBUI_EVENT_UPDATED, /* a firmware upload finished: restart into it */
    WEBUI_EVENT_FACTORY_RESET,
    WEBUI_EVENT_SESSION, /* someone logged in or out: see webui_session_active() */
} webui_event_t;

typedef struct {
    /* Runs fn(arg) on the app task and waits for it (the app's executor). */
    esp_err_t (*run)(void (*fn)(void *arg), void *arg);
    webui_api_fn api;
    /* Called on the server's task; the app must not stop the server from inside it. */
    void (*event)(webui_event_t event);
} webui_config_t;

#define WEBUI_BODY_MAX  (96 * 1024) /* the largest request: a backup to restore, its presets up to 48 KB (M6c) and
                                       its MQTT fields up to 28 KB (M7) */
#define WEBUI_REPLY_MAX (96 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
/* The deepest request: a backup bundle, a file's own 20 levels inside the bundle's two
 * (BACKUP_MAX_DEPTH, storage_backup.h). Deeper ones are refused before anything parses them. */
#define WEBUI_JSON_MAX_DEPTH 22

esp_err_t webui_start(const webui_config_t *config);
void webui_stop(void);
bool webui_running(void);
/* When the last request came in (esp_timer ms); config mode ends 10 min after it (spec §10.2). */
int64_t webui_last_request_ms(void);
bool webui_password_set(void);
/* A phone is logged in to the web UI (D20). */
bool webui_session_active(void);
/* Menu ▸ Wi-Fi ▸ Reset web password (D18): the next visit, over the AP, chooses a new one. */
esp_err_t webui_reset_password(void);
