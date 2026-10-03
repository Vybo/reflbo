#include "ha_mqtt.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ha_session.h"
#include "mqtt_client.h"
#include "nvs.h"

static const char *TAG = "ha_mqtt";

#define TASK_STACK 4096
#define TASK_PRIORITY 3 /* below netmgr (4) and the app (5), as the sync's task */
#define QUEUE_DEPTH 8
#define MQTT_TASK_STACK 6144 /* the values' JSON parses on it */
#define MQTT_TASK_PRIORITY 3
#define MQTT_BUFFER 4096 /* a mapped payload longer than this is ignored */
#define KEEPALIVE_S 60
#define CONNECT_MAX_MS 10000
#define STEP_MS 50
#define ACKS_AHEAD 4 /* QoS 1 messages in flight at once */
#define SUBSCRIBE_CHUNK 8
#define NVS_KEY_DISC "mqtt_disc" /* in `sys`: the hash of the discovery configs that went out (spec §12.3) */

#define BIT_CONNECTED BIT0
#define BIT_DOWN BIT1

typedef enum { REQ_SESSION, REQ_KEEP, REQ_DROP, REQ_STATE, REQ_TEST, REQ_FIELDS, REQ_UP, REQ_DOWN } req_type_t;

typedef struct {
    req_type_t type;
    uint32_t gen; /* REQ_UP, REQ_DOWN: the generation of the client whose event it is */
    ha_conn_t *conn; /* a copy, freed by the task */
    char *text;
    int budget_ms;
} req_t;

static ha_hooks_t s_hooks;
static bool s_started;
static QueueHandle_t s_queue;
/* The mappings, the topics seen and the details. esp-mqtt's task holds the client's API lock while it hands
 * us events, and on_data() takes s_lock: so nothing calls esp-mqtt, or a hook, with s_lock held. */
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_bits;
static esp_mqtt_client_handle_t s_client; /* the task's; set under s_client_lock, which the app's key presses take */
static SemaphoreHandle_t s_client_lock;
static volatile uint32_t s_gen;            /* the client's generation: a closed client's late events are ignored */
static ha_conn_t s_conn;                   /* what s_client connects with */
static volatile bool s_connected, s_keep;
static volatile bool s_session_present;    /* the broker kept our session: it has seen the discovery configs */
static atomic_int s_subacks, s_pubacks;    /* awaited */
static int s_failures;
static int64_t s_retry_at_ms;
static char s_detail[HA_DETAIL_LEN];
EXT_RAM_BSS_ATTR static ha_fields_t s_fields;
static const char *s_topics[HA_FIELDS_MAX];
static int s_topic_count;
EXT_RAM_BSS_ATTR static char s_filters[HA_FIELDS_MAX + 1][HA_TOPIC_LEN]; /* the task's copy, to subscribe */
EXT_RAM_BSS_ATTR static struct {
    char key[HA_KEY_LEN];
    ha_value_t v;
} s_got[HA_FIELDS_MAX]; /* esp-mqtt's task: a message's values, handed over once s_lock is free */
static uint32_t s_seen;          /* a bit per topic: a message came this session */
static int64_t s_last_msg_ms;
EXT_RAM_BSS_ATTR static ha_payloads_t s_payloads;
static struct {
    SemaphoreHandle_t done;
    esp_err_t result;
    char detail[HA_DETAIL_LEN];
} s_session;
static struct {
    volatile bool running, done, ok;
    char detail[HA_DETAIL_LEN];
} s_test;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void set_detail(char *to, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(to, HA_DETAIL_LEN, "%s", text);
    xSemaphoreGive(s_lock);
}

static bool post(const req_t *r)
{
    if (!s_started || xQueueSend(s_queue, r, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "%s: request %d dropped", s_started ? "queue full" : "not started", r->type);
        free(r->conn);
        free(r->text);
        return false;
    }
    return true;
}

/* Why a connection failed, in a few words (spec §12.9). */
static void error_detail(const esp_mqtt_error_codes_t *e, char *out, size_t size)
{
    if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        esp_mqtt_connect_return_code_t c = e->connect_return_code;
        snprintf(out, size, "%s", c == MQTT_CONNECTION_REFUSE_BAD_USERNAME || c == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED
                                      ? "refused: login"
                                  : c == MQTT_CONNECTION_REFUSE_ID_REJECTED ? "refused: client id"
                                                                            : "refused");
    } else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
        snprintf(out, size, "host not found");
    } else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
        snprintf(out, size, "timeout");
    } else if (e->esp_transport_sock_errno == ECONNREFUSED) {
        snprintf(out, size, "no broker");
    } else if (e->esp_transport_sock_errno != 0) {
        snprintf(out, size, "socket error %d", e->esp_transport_sock_errno);
    } else {
        snprintf(out, size, "no connection");
    }
}

/* A message: a command, or a mapped topic's value for each field that reads it. */
static void on_data(esp_mqtt_event_handle_t e)
{
    if (e->current_data_offset != 0 || e->data_len != e->total_data_len) {
        if (e->current_data_offset == 0) { /* its other pieces come without the topic */
            ESP_LOGW(TAG, "%.*s: %d bytes is too long; ignored", e->topic_len, e->topic, e->total_data_len);
        }
        return;
    }
    ha_cmd_t cmd = ha_cmd_parse(s_conn.id, e->topic, (size_t)e->topic_len);
    if (cmd != HA_CMD_NONE) {
        if (e->retain) { /* commands aren't retained (spec §12.2): one would come back at every session */
            ESP_LOGW(TAG, "a retained command on %.*s: ignored", e->topic_len, e->topic);
        } else {
            s_hooks.command(cmd, e->data, (size_t)e->data_len);
        }
        return;
    }
    int got = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int t = ha_topic_index(s_topics, s_topic_count, e->topic, (size_t)e->topic_len);
    if (t >= 0) {
        s_seen |= 1u << t;
        s_last_msg_ms = now_ms();
        for (int i = 0; i < s_fields.count && got < HA_FIELDS_MAX; i++) {
            if (strcmp(s_fields.field[i].topic, s_topics[t]) == 0 &&
                ha_value_parse(&s_fields.field[i], e->data, (size_t)e->data_len, &s_got[got].v)) {
                snprintf(s_got[got++].key, HA_KEY_LEN, "%s", s_fields.field[i].key);
            }
        }
    }
    xSemaphoreGive(s_lock);
    for (int i = 0; i < got; i++) {
        s_hooks.value(s_got[i].key, &s_got[i].v);
    }
}

/* On esp-mqtt's task. */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    esp_mqtt_event_handle_t e = data;
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    if (gen != s_gen) {
        return; /* a client being closed: its news is no longer ours */
    }
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_session_present = e->session_present;
        s_connected = true;
        xEventGroupSetBits(s_bits, BIT_CONNECTED);
        post(&(req_t){ .type = REQ_UP, .gen = gen });
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        xEventGroupSetBits(s_bits, BIT_DOWN);
        post(&(req_t){ .type = REQ_DOWN, .gen = gen });
        break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle != NULL && e->error_handle->error_type != MQTT_ERROR_TYPE_NONE) {
            char detail[HA_DETAIL_LEN];
            error_detail(e->error_handle, detail, sizeof(detail));
            set_detail(s_detail, detail);
            xEventGroupSetBits(s_bits, BIT_DOWN);
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        atomic_fetch_sub(&s_subacks, 1);
        break;
    case MQTT_EVENT_PUBLISHED:
        atomic_fetch_sub(&s_pubacks, 1);
        break;
    case MQTT_EVENT_DATA:
        on_data(e);
        break;
    default:
        break;
    }
}

static esp_mqtt_client_handle_t open_client(const ha_conn_t *c, int timeout_ms)
{
    const esp_mqtt_client_config_t cfg = {
        .broker.address = { .hostname = c->host, .port = c->port, .transport = MQTT_TRANSPORT_OVER_TCP },
        .credentials = { .client_id = c->id, .username = c->user[0] ? c->user : NULL,
                         .authentication.password = c->password[0] ? c->password : NULL },
        .session = { .disable_clean_session = true, .keepalive = KEEPALIVE_S }, /* commands wait (spec §12.4) */
        .network = { .disable_auto_reconnect = true, .timeout_ms = timeout_ms },
        .task = { .priority = MQTT_TASK_PRIORITY, .stack_size = MQTT_TASK_STACK },
        .buffer = { .size = MQTT_BUFFER },
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    if (client == NULL) {
        set_detail(s_detail, "no memory");
        return NULL;
    }
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_event, (void *)(uintptr_t)++s_gen);
    xEventGroupClearBits(s_bits, BIT_CONNECTED | BIT_DOWN);
    set_detail(s_detail, "");
    s_connected = false;
    if (esp_mqtt_client_start(client) != ESP_OK) {
        esp_mqtt_client_destroy(client);
        set_detail(s_detail, "no client");
        return NULL;
    }
    return client;
}

static void set_client(esp_mqtt_client_handle_t client)
{
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    s_client = client;
    xSemaphoreGive(s_client_lock);
}

static void close_client(void)
{
    s_gen++; /* its events from now on are ignored, the CONNECTED of a connect that ends during the stop too */
    esp_mqtt_client_handle_t client = s_client;
    set_client(NULL);
    s_connected = false;
    if (client != NULL) {
        esp_mqtt_client_stop(client); /* a DISCONNECT first: the broker keeps the session (spec §12.4) */
        esp_mqtt_client_destroy(client);
    }
}

/* Waits for the connection; false with the reason in s_detail. */
static bool wait_connected(int64_t end_ms)
{
    int64_t left = end_ms - now_ms();
    EventBits_t bits = xEventGroupWaitBits(s_bits, BIT_CONNECTED | BIT_DOWN, pdFALSE, pdFALSE,
                                           left > 0 ? pdMS_TO_TICKS(left) : 0);
    if (bits & BIT_CONNECTED && s_connected) {
        return true;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_detail[0] == '\0') {
        snprintf(s_detail, sizeof(s_detail), "%s", bits & BIT_DOWN ? "no connection" : "timeout");
    }
    xSemaphoreGive(s_lock);
    return false;
}

static void wait_acks(atomic_int *acks, int at_most, int64_t end_ms)
{
    while (atomic_load(acks) > at_most && s_connected && now_ms() < end_ms) {
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

/* cmd/# at QoS 1 and the mapped topics at QoS 0, in chunks, then their acknowledgements. The filters are
 * copied under s_lock and subscribed without it (see s_lock). */
static bool subscribe(int64_t end_ms)
{
    ha_topic(s_filters[0], HA_TOPIC_LEN, s_conn.id, "cmd/#");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_topic_count = ha_topics(&s_fields, s_topics);
    s_seen = 0;
    for (int i = 0; i < s_topic_count; i++) {
        snprintf(s_filters[i + 1], HA_TOPIC_LEN, "%s", s_topics[i]);
    }
    int total = s_topic_count + 1;
    xSemaphoreGive(s_lock);
    esp_mqtt_topic_t list[SUBSCRIBE_CHUNK];
    bool ok = true;
    for (int at = 0; ok && at < total; at += SUBSCRIBE_CHUNK) {
        int n = 0;
        for (int i = at; i < total && n < SUBSCRIBE_CHUNK; i++, n++) {
            list[n] = (esp_mqtt_topic_t){ .filter = s_filters[i], .qos = i == 0 ? 1 : 0 };
        }
        atomic_fetch_add(&s_subacks, 1);
        ok = esp_mqtt_client_subscribe_multiple(s_client, list, n) >= 0;
        if (!ok) {
            atomic_fetch_sub(&s_subacks, 1);
        }
    }
    wait_acks(&s_subacks, 0, end_ms);
    ok = ok && atomic_load(&s_subacks) <= 0;
    atomic_store(&s_subacks, 0);
    if (!ok) {
        set_detail(s_detail, "subscribe failed");
    }
    return ok;
}

/* spec §9.3 step 6.3: until every mapped topic brought its retained value, or a quiet second. */
static void collect(int64_t end_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_msg_ms = now_ms();
    xSemaphoreGive(s_lock);
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int seen = __builtin_popcount(s_seen);
        bool over = ha_collect_over(s_topic_count, seen, now_ms(), s_last_msg_ms);
        xSemaphoreGive(s_lock);
        if (over || now_ms() >= end_ms) {
            ESP_LOGI(TAG, "%d of %d topics came", seen, s_topic_count);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

static uint32_t disc_hash_saved(void)
{
    nvs_handle_t nvs;
    uint32_t hash = 0;
    if (nvs_open("sys", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, NVS_KEY_DISC, &hash);
        nvs_close(nvs);
    }
    return hash;
}

static void disc_hash_save(uint32_t hash)
{
    nvs_handle_t nvs;
    if (nvs_open("sys", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if ((hash != 0 ? nvs_set_u32(nvs, NVS_KEY_DISC, hash) : nvs_erase_key(nvs, NVS_KEY_DISC)) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

static bool publish_qos1(const char *topic, const char *payload, int64_t end_ms)
{
    wait_acks(&s_pubacks, ACKS_AHEAD - 1, end_ms);
    atomic_fetch_add(&s_pubacks, 1);
    if (esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1) < 0) {
        atomic_fetch_sub(&s_pubacks, 1);
        return false;
    }
    return true;
}

/* The state, and the discovery configs whose hash changed (spec §12.3), retained at QoS 1. */
static bool publish_payloads(int64_t end_ms)
{
    if (!s_hooks.payloads(&s_payloads)) {
        set_detail(s_detail, "app busy");
        return false;
    }
    atomic_store(&s_pubacks, 0);
    /* again when they changed, or when the broker has no session of ours: a new broker, or one that lost its
     * state, may have lost the retained configs too */
    bool disc = s_conn.discovery && (!s_session_present || s_payloads.disc_hash != disc_hash_saved());
    bool ok = true;
    for (int i = 0; disc && ok && i < HA_DISC_COUNT; i++) {
        if (s_payloads.topic[i][0] != '\0') { /* the app leaves out a config that didn't fit */
            ok = publish_qos1(s_payloads.topic[i], s_payloads.payload[i], end_ms);
        }
    }
    char topic[HA_TOPIC_MAX];
    ha_topic(topic, sizeof(topic), s_conn.id, "state");
    ok = ok && publish_qos1(topic, s_payloads.state, end_ms);
    wait_acks(&s_pubacks, 0, end_ms);
    ok = ok && atomic_load(&s_pubacks) <= 0;
    atomic_store(&s_pubacks, 0);
    if (!ok) {
        set_detail(s_detail, s_connected ? "publish timed out" : "connection lost");
    } else if (disc) {
        disc_hash_save(s_payloads.disc_hash);
        ESP_LOGI(TAG, "discovery published");
    }
    return ok;
}

static void run_session(const ha_conn_t *c, int budget_ms)
{
    int64_t end = now_ms() + budget_ms;
    bool ok;
    if (s_keep) { /* sync mode `always`: the kept client (spec §12.9) */
        ok = s_connected && publish_payloads(end);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_connected && s_detail[0] == '\0') {
            snprintf(s_detail, sizeof(s_detail), "not connected");
        }
        xSemaphoreGive(s_lock);
    } else {
        s_conn = *c;
        set_client(open_client(c, budget_ms < CONNECT_MAX_MS ? budget_ms : CONNECT_MAX_MS));
        ok = s_client != NULL && wait_connected(end) && subscribe(end);
        if (ok) {
            collect(end);
            ok = publish_payloads(end);
        }
        close_client();
    }
    s_session.result = ok ? ESP_OK : ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_session.detail, sizeof(s_session.detail), "%s", ok ? "" : s_detail);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "session %s%s", ok ? "done" : "failed: ", ok ? "" : s_session.detail);
}

static void keep(const ha_conn_t *c)
{
    close_client();
    s_keep = true;
    s_failures = 0;
    s_retry_at_ms = 0;
    s_conn = *c;
    set_client(open_client(c, CONNECT_MAX_MS));
    if (s_client == NULL) {
        s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures++);
    }
}

static void test(const ha_conn_t *c)
{
    bool ok;
    if (s_keep) {
        ok = wait_connected(now_ms() + CONNECT_MAX_MS); /* the kept client is the test, maybe still connecting */
    } else {
        ha_conn_t saved = s_conn;
        s_conn = *c;
        set_client(open_client(c, CONNECT_MAX_MS));
        ok = s_client != NULL && wait_connected(now_ms() + CONNECT_MAX_MS);
        close_client();
        s_conn = saved;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_test.detail, sizeof(s_test.detail), "%s", ok ? "" : s_detail[0] ? s_detail : "no connection");
    xSemaphoreGive(s_lock);
    s_test.ok = ok;
    s_test.done = true;
    s_test.running = false;
    ESP_LOGI(TAG, "test %s%s", ok ? "connected" : "failed: ", ok ? "" : s_test.detail);
}

static void handle(req_t *r)
{
    switch (r->type) {
    case REQ_SESSION:
        run_session(r->conn, r->budget_ms);
        xSemaphoreGive(s_session.done);
        break;
    case REQ_KEEP:
        keep(r->conn);
        break;
    case REQ_DROP:
        s_keep = false;
        s_retry_at_ms = 0;
        close_client();
        s_hooks.status();
        break;
    case REQ_STATE:
        if (s_connected && s_client != NULL) {
            char topic[HA_TOPIC_MAX];
            ha_topic(topic, sizeof(topic), s_conn.id, "state");
            esp_mqtt_client_publish(s_client, topic, r->text, 0, 1, 1);
        }
        break;
    case REQ_TEST:
        test(r->conn);
        break;
    case REQ_FIELDS: /* new mappings, or the retained values again */
        if (s_keep && s_connected) {
            subscribe(now_ms() + CONNECT_MAX_MS);
        }
        break;
    case REQ_UP:
        if (r->gen == s_gen && s_keep) { /* spec §12.9: subscriptions, then the state and any discovery */
            s_failures = 0;
            int64_t end = now_ms() + CONNECT_MAX_MS;
            if (subscribe(end)) {
                publish_payloads(end);
            }
            s_hooks.status();
        }
        break;
    case REQ_DOWN:
        if (r->gen == s_gen && s_keep) {
            s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures);
            char why[HA_DETAIL_LEN];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            snprintf(why, sizeof(why), "%s", s_detail);
            xSemaphoreGive(s_lock);
            ESP_LOGW(TAG, "connection lost (%s); again in %u s", why, (unsigned)(ha_backoff_ms(s_failures) / 1000));
            s_failures++;
            s_hooks.status();
        }
        break;
    }
    free(r->conn);
    free(r->text);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (s_keep && !s_connected && s_retry_at_ms != 0) {
            int64_t left = s_retry_at_ms - now_ms();
            wait = left > 0 ? pdMS_TO_TICKS(left) + 1 : 0;
        }
        req_t r;
        if (xQueueReceive(s_queue, &r, wait) == pdTRUE) {
            handle(&r);
        } else if (s_keep && !s_connected && s_retry_at_ms != 0 && now_ms() >= s_retry_at_ms) {
            s_retry_at_ms = 0;
            if (s_client == NULL) {
                keep(&s_conn);
            } else if (esp_mqtt_client_reconnect(s_client) != ESP_OK) {
                s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures++);
            }
        }
    }
}

esp_err_t ha_mqtt_init(const ha_hooks_t *hooks)
{
    if (s_started) {
        return ESP_OK;
    }
    s_hooks = *hooks;
    /* each made once: after a failure, a later call makes what is still missing */
    s_lock = s_lock != NULL ? s_lock : xSemaphoreCreateMutex();
    s_client_lock = s_client_lock != NULL ? s_client_lock : xSemaphoreCreateMutex();
    s_bits = s_bits != NULL ? s_bits : xEventGroupCreate();
    s_session.done = s_session.done != NULL ? s_session.done : xSemaphoreCreateBinary();
    s_queue = s_queue != NULL ? s_queue : xQueueCreate(QUEUE_DEPTH, sizeof(req_t));
    if (s_lock == NULL || s_client_lock == NULL || s_bits == NULL || s_session.done == NULL || s_queue == NULL ||
        xTaskCreatePinnedToCore(task, "ha_mqtt", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

void ha_mqtt_set_fields(const ha_fields_t *f)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_fields = *f;
    xSemaphoreGive(s_lock);
    post(&(req_t){ .type = REQ_FIELDS });
}

void ha_mqtt_resubscribe(void)
{
    post(&(req_t){ .type = REQ_FIELDS });
}

static ha_conn_t *copy(const ha_conn_t *c)
{
    ha_conn_t *out = malloc(sizeof(*out));
    if (out != NULL) {
        *out = *c;
    }
    return out;
}

esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_t size)
{
    if (!s_started) {
        snprintf(detail, size, "no client");
        return ESP_ERR_INVALID_STATE;
    }
    ha_conn_t *conn = copy(c);
    if (conn == NULL) {
        snprintf(detail, size, "no memory");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_session.done, 0); /* a late answer to an earlier session */
    if (!post(&(req_t){ .type = REQ_SESSION, .conn = conn, .budget_ms = budget_ms })) {
        snprintf(detail, size, "busy");
        return ESP_FAIL;
    }
    if (xSemaphoreTake(s_session.done, pdMS_TO_TICKS(budget_ms + 5000)) != pdTRUE) {
        snprintf(detail, size, "timeout");
        return ESP_ERR_TIMEOUT;
    }
    snprintf(detail, size, "%s", s_session.detail);
    return s_session.result;
}

void ha_mqtt_keep(const ha_conn_t *c)
{
    ha_conn_t *conn = copy(c);
    if (conn != NULL) {
        post(&(req_t){ .type = REQ_KEEP, .conn = conn });
    }
}

void ha_mqtt_drop(void)
{
    post(&(req_t){ .type = REQ_DROP });
}

void ha_mqtt_publish_state(const char *json)
{
    if (!s_connected) {
        return;
    }
    char *text = strdup(json);
    if (text != NULL) {
        post(&(req_t){ .type = REQ_STATE, .text = text });
    }
}

/* On the app task, at once: a sync's session keeps the client's task busy until it ends (spec §12.8). Once
 * connected, esp-mqtt holds its lock only briefly, and a QoS 0 publish is a write. */
void ha_mqtt_publish_action(const char *payload)
{
    if (s_client_lock == NULL || payload == NULL) {
        return;
    }
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    if (s_connected && s_client != NULL) {
        char topic[HA_TOPIC_MAX];
        ha_topic(topic, sizeof(topic), s_conn.id, "action");
        esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
        ESP_LOGI(TAG, "key press: %s", payload);
    }
    xSemaphoreGive(s_client_lock);
}

void ha_mqtt_test(const ha_conn_t *c)
{
    ha_conn_t *conn = copy(c);
    if (conn == NULL) {
        return;
    }
    s_test.running = true;
    s_test.done = false;
    if (!post(&(req_t){ .type = REQ_TEST, .conn = conn })) {
        s_test.running = false;
    }
}

void ha_mqtt_forget_discovery(void)
{
    disc_hash_save(0);
}

uint32_t ha_mqtt_discovery_hash(void)
{
    return disc_hash_saved();
}

void ha_mqtt_status(ha_mqtt_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_started) {
        return;
    }
    out->keep = s_keep;
    out->connected = s_connected;
    out->test_running = s_test.running;
    out->test_done = s_test.done;
    out->test_ok = s_test.ok;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out->detail, sizeof(out->detail), "%s", s_detail);
    snprintf(out->test_detail, sizeof(out->test_detail), "%s", s_test.detail);
    xSemaphoreGive(s_lock);
}
