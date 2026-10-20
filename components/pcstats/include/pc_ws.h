#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * The stream to the PC app (spec §12a.3, M7, D44): a WebSocket to ws://<host>:<port>/v1/stream with the token, on
 * esp_websocket_client's task. Each event goes to the callback on that task, which hands it on without blocking.
 * One stream at a time; it doesn't reconnect by itself, as the session decides (spec §12a.3). Device only.
 */

typedef enum {
    PC_WS_CONNECTED, /* say hello */
    PC_WS_FRAME,     /* a text message, whole */
    PC_WS_CLOSED,    /* closed, refused or lost */
    PC_WS_TOO_BIG,   /* a message over PC_FRAME_MAX: close it (spec §12a.3) */
} pc_ws_event_t;

typedef struct {
    pc_ws_event_t ev;
    uint32_t gen;    /* one opening's events from the next one's */
    char *frame;     /* PC_WS_FRAME: NUL-terminated, `len` bytes, in PSRAM; the callee's to free() */
    size_t len;
    int close_code;  /* PC_WS_CLOSED: the PC app's close code (1001 quit, 1008 refused), 0 for none */
    int http_status; /* PC_WS_CLOSED: the upgrade's status when it was refused (401), 0 otherwise */
    char reason[32]; /* PC_WS_CLOSED: the close's reason ("new token") */
} pc_ws_msg_t;

typedef void (*pc_ws_fn)(const pc_ws_msg_t *msg);

/* Opens the stream, closing the one before; the generation of its events in *gen. */
esp_err_t pc_ws_open(const char *host, uint16_t port, const char *token, pc_ws_fn fn, uint32_t *gen);
/* A text frame: the hello. */
esp_err_t pc_ws_send(const char *text, size_t len);
/* Closes it without waiting: a task of its own closes the connection and frees it. Its events may still come, with
 * its generation. */
void pc_ws_close(void);
