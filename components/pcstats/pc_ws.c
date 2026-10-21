#include "pc_ws.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc_msg.h"

/* The stream to the PC app (spec §12a.3). A message is taken whole, at most 8 KB, in PSRAM, before it leaves the
 * client's task; each connection has its own buffer, as a closing one's task can still run beside the next. */

static const char *TAG = "pc_ws";

#define TASK_STACK 4096
#define TASK_PRIORITY 3 /* as the sync's: below netmgr (4) and the app (5) */
#define CLOSER_STACK 3072
#define CLOSER_PRIORITY 2
#define NETWORK_TIMEOUT_MS 5000
#define CLOSE_WAIT_MS 1000
#define OP_CONTINUATION 0x00
#define OP_TEXT 0x01
#define OP_CLOSE 0x08

typedef struct {
    esp_websocket_client_handle_t client;
    uint32_t gen;
    pc_ws_fn fn;
    char *buf; /* the message being taken, PC_FRAME_MAX + 1 */
    size_t have;
    bool taking, too_big, told;
    int close_code, http_status;
    char reason[32];
} conn_t;

static conn_t *s_conn; /* the open one; the app task's */
static uint32_t s_gen;

static void tell(conn_t *c, pc_ws_event_t ev, char *frame, size_t len)
{
    pc_ws_msg_t m = { .ev = ev, .gen = c->gen, .frame = frame, .len = len };
    if (ev == PC_WS_CLOSED) {
        m.close_code = c->close_code;
        m.http_status = c->http_status;
        snprintf(m.reason, sizeof(m.reason), "%s", c->reason);
    }
    c->fn(&m);
}

/* A data event: a close's code and reason, or a text message's part. A message is a text frame and its continuations;
 * a frame longer than the client's buffer comes in parts too (`payload_offset`). */
static void take(conn_t *c, const esp_websocket_event_data_t *d)
{
    if (d->op_code == OP_CLOSE) {
        if (d->data_len >= 2) {
            c->close_code = ((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1];
            int n = d->data_len - 2 < (int)sizeof(c->reason) - 1 ? d->data_len - 2 : (int)sizeof(c->reason) - 1;
            memcpy(c->reason, d->data_ptr + 2, (size_t)n);
            c->reason[n] = '\0';
        }
        return;
    }
    if (d->op_code == OP_TEXT && d->payload_offset == 0) {
        c->taking = true; /* a new message */
        c->have = 0;
        c->too_big = false;
    } else if (d->op_code != OP_CONTINUATION && d->op_code != OP_TEXT) {
        return; /* pings and pongs: the client answers them */
    }
    if (!c->taking) {
        return; /* a continuation of nothing */
    }
    if (!c->too_big && c->have + (size_t)d->data_len > PC_FRAME_MAX) {
        c->too_big = true;
    }
    if (!c->too_big) {
        memcpy(c->buf + c->have, d->data_ptr, (size_t)d->data_len);
        c->have += (size_t)d->data_len;
    }
    if (!d->fin || d->payload_offset + d->data_len < d->payload_len) {
        return; /* more of it to come */
    }
    c->taking = false;
    if (c->too_big) {
        tell(c, PC_WS_TOO_BIG, NULL, 0);
        return;
    }
    char *copy = heap_caps_malloc(c->have + 1, MALLOC_CAP_SPIRAM);
    if (copy == NULL) {
        ESP_LOGW(TAG, "no memory for a message of %u bytes", (unsigned)c->have);
        return;
    }
    memcpy(copy, c->buf, c->have);
    copy[c->have] = '\0';
    tell(c, PC_WS_FRAME, copy, c->have);
}

static void handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    conn_t *c = arg;
    const esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        c->taking = c->told = false;
        c->close_code = c->http_status = 0;
        c->reason[0] = '\0';
        tell(c, PC_WS_CONNECTED, NULL, 0);
        break;
    case WEBSOCKET_EVENT_DATA:
        take(c, d);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (c->http_status == 0) { /* 401: a wrong token; the library never sets the handshake error's type */
            c->http_status = pc_upgrade_status(d->error_handle.esp_ws_handshake_status_code);
        }
        if (!c->told) {
            c->told = true;
            tell(c, PC_WS_CLOSED, NULL, 0);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        if (c->close_code == 0) {
            c->close_code = d->close_status_code;
        }
        if (c->http_status == 0) {
            c->http_status = pc_upgrade_status(d->error_handle.esp_ws_handshake_status_code);
        }
        if (!c->told) {
            c->told = true;
            tell(c, PC_WS_CLOSED, NULL, 0);
        }
        break;
    default:
        break;
    }
}

static void conn_free(conn_t *c)
{
    if (c->client != NULL) {
        if (esp_websocket_client_is_connected(c->client)) {
            esp_websocket_client_close(c->client, pdMS_TO_TICKS(CLOSE_WAIT_MS));
        }
        esp_websocket_client_destroy(c->client); /* stops its task: no event after this */
    }
    heap_caps_free(c->buf);
    free(c);
}

static void closer(void *arg) /* a task of its own, so the app task never waits on a connection */
{
    conn_free(arg);
    vTaskDelete(NULL);
}

void pc_ws_close(void)
{
    conn_t *c = s_conn;
    if (c == NULL) {
        return;
    }
    s_conn = NULL;
    if (xTaskCreatePinnedToCore(closer, "pc_ws_close", CLOSER_STACK, c, CLOSER_PRIORITY, NULL, 0) != pdPASS) {
        conn_free(c); /* no task for it: here, then */
    }
    ESP_LOGI(TAG, "closing");
}

esp_err_t pc_ws_open(const char *host, uint16_t port, const char *token, pc_ws_fn fn, uint32_t *gen)
{
    pc_ws_close();
    conn_t *c = calloc(1, sizeof(*c));
    char *buf = heap_caps_malloc(PC_FRAME_MAX + 1, MALLOC_CAP_SPIRAM);
    if (c == NULL || buf == NULL) {
        free(c);
        heap_caps_free(buf);
        return ESP_ERR_NO_MEM;
    }
    char uri[96];
    snprintf(uri, sizeof(uri), "ws://%s:%u/v1/stream", host, (unsigned)port);
    char headers[96]; /* the token as typed: up to 63 characters (spec §12a.1) */
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .headers = headers,
        .disable_auto_reconnect = true, /* the session reconnects once, then ends (spec §12a.3) */
        .network_timeout_ms = NETWORK_TIMEOUT_MS,
        .task_stack = TASK_STACK,
        .task_prio = TASK_PRIORITY,
        .buffer_size = 1024,
    };
    *c = (conn_t){ .gen = ++s_gen, .fn = fn, .buf = buf };
    c->client = esp_websocket_client_init(&cfg);
    if (c->client == NULL) {
        conn_free(c);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_websocket_register_events(c->client, WEBSOCKET_EVENT_ANY, handler, c);
    if (err == ESP_OK) {
        err = esp_websocket_client_start(c->client);
    }
    if (err != ESP_OK) {
        conn_free(c);
        return err;
    }
    s_conn = c;
    *gen = c->gen;
    ESP_LOGI(TAG, "opening %s:%u", host, (unsigned)port);
    return ESP_OK;
}

esp_err_t pc_ws_send(const char *text, size_t len)
{
    if (s_conn == NULL || !esp_websocket_client_is_connected(s_conn->client)) {
        return ESP_ERR_INVALID_STATE;
    }
    int n = esp_websocket_client_send_text(s_conn->client, text, (int)len, pdMS_TO_TICKS(CLOSE_WAIT_MS));
    return n == (int)len ? ESP_OK : ESP_FAIL;
}
