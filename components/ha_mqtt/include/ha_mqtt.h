#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ha_fields.h"
#include "ha_payload.h"

/*
 * The MQTT client (spec §12.9, D32): one esp-mqtt client, driven from a task of its own. A sync runs a
 * session through it (ha_mqtt_session(), which blocks the sync's task); sync mode `always` keeps it
 * connected (ha_mqtt_keep()). Commands and the mapped topics' values go to the app through hooks; the state
 * and the discovery configs come from the app, which builds them on its own task. The commands' topic is
 * subscribed at QoS 1, so the broker keeps them for the sleeping device in its persistent session; the mapped
 * topics at QoS 0, so it keeps no backlog of readings, and each subscription brings their retained value.
 * Device only.
 */

#define HA_DETAIL_LEN 24 /* why a session failed: "no broker", "refused: login" */
#define HA_PASS_LEN 64
#define HA_STATE_MAX 512

typedef struct {
    char id[16]; /* reflbo-XXXX: the client id, and the node of every topic */
    char host[64];
    uint16_t port;
    char user[64];
    char password[HA_PASS_LEN];
    bool discovery;
} ha_conn_t;

/* What a session publishes; the app fills it on its task. */
typedef struct {
    char state[HA_STATE_MAX];
    uint32_t disc_hash; /* ha_disc_hash() */
    char topic[HA_DISC_COUNT][HA_TOPIC_MAX];
    char payload[HA_DISC_COUNT][HA_PAYLOAD_MAX];
} ha_payloads_t;

typedef struct {
    /* On the client's tasks: they hand over to the app task and return at once. */
    void (*command)(ha_cmd_t cmd, const char *payload, size_t len);
    void (*value)(const char *key, const ha_value_t *v);
    void (*status)(void); /* the connection came or went */
    /* Fills `out`; the client's task waits while the app task builds it. False if it couldn't. */
    bool (*payloads)(ha_payloads_t *out);
} ha_hooks_t;

typedef struct {
    bool keep;      /* sync mode `always` keeps the client */
    bool connected;
    char detail[HA_DETAIL_LEN]; /* why the last connection failed */
    bool test_running, test_done, test_ok;
    char test_detail[HA_DETAIL_LEN];
} ha_mqtt_status_t;

/* Starts the client's task; once. */
esp_err_t ha_mqtt_init(const ha_hooks_t *hooks);
/* The mappings changed (a cold boot, the MQTT page, a restore): a kept connection subscribes again. */
void ha_mqtt_set_fields(const ha_fields_t *f);
/* A sync's step (spec §9.3 step 6): connect, subscribe, collect, publish the state and any discovery
 * configs whose hash changed, and disconnect, within `budget_ms`; with the client kept, publish on it. ESP_OK,
 * or ESP_FAIL with why in `detail`. */
esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_t size);
/* Sync mode `always` (spec §12.9): connected with `c`, again after 10 s doubling to 5 min when it drops. */
void ha_mqtt_keep(const ha_conn_t *c);
void ha_mqtt_drop(void);
void ha_mqtt_publish_state(const char *json);     /* while connected: retained, QoS 1 */
void ha_mqtt_publish_action(const char *payload); /* while connected: reflbo/<id>/action, QoS 0 (spec §12.8) */
/* Test connection (spec §10.3): connects with `c` and leaves; ha_mqtt_status() reports how it went. */
void ha_mqtt_test(const ha_conn_t *c);
/* Discovery was turned off: the next time it is on, the configs go out again (NVS sys/mqtt_disc). */
void ha_mqtt_forget_discovery(void);
void ha_mqtt_status(ha_mqtt_status_t *out);
