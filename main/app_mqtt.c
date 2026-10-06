#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "app_internal.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ha_mqtt.h"
#include "ha_session.h"
#include "ha_store.h"
#include "lang.h"
#include "netmgr.h"
#include "storage.h"
#include "sync_plan.h"
#include "timekeeping.h"

/* MQTT and Home Assistant on the app's side (spec §12, D32): the client's hooks, the sync's step, sync mode
 * `always`'s connection and its state, the mapped fields' values and the message, the key presses HA hears,
 * and what the status bar and Info say about them. It belongs to the app task, but for the hooks, which run
 * on the client's and the sync's tasks and hand over to it. */

static const char *TAG = "app_mqtt";

_Static_assert(HA_PASS_LEN >= SETTINGS_SECRET_LEN, "the broker's password fits the client's");

#define CHECK_MS 30000        /* sync mode `always`: how often the state is looked at (spec §12.9) */
#define RESUBSCRIBE_MS 300000 /* sync mode `always`: the retained values again, with the 5-min state */

static bool s_started;           /* the client's task runs */
static ha_conn_t s_conn;         /* the next session's: set on the app task as a sync starts */
static bool s_keeping;           /* sync mode `always` keeps the client */
static int64_t s_published_ms = -1;
static int64_t s_check_ms;
static int64_t s_resubscribe_ms;
static bool s_state_now;         /* a command changed the state: it goes out at once (spec §12.4) */
EXT_RAM_BSS_ATTR static char s_published[HA_STATE_MAX]; /* the last state that went out, its uptime left out */

/* The values and the message through deep sleep (spec §12.5): RTC FAST memory, kept powered as the heap
 * may use it (CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP), since the snapshot fills RTC SLOW. */
static RTC_FAST_ATTR ha_store_t s_store;
_Static_assert(sizeof(ha_store_t) <= 4096, "the MQTT values' RTC block is at most 4 KB");
EXT_RAM_BSS_ATTR static ha_fields_t s_fields; /* the mappings, read once a boot when something needs them */
EXT_RAM_BSS_ATTR static char s_fields_json[HA_FIELDS_JSON_MAX];
static bool s_fields_loaded, s_fields_sent;

/* Values from the client's task wait here for the app task, so a burst of retained values draws once. */
typedef struct {
    char key[HA_KEY_LEN];
    ha_value_t v;
} staged_t;
static SemaphoreHandle_t s_stage_lock;
EXT_RAM_BSS_ATTR static staged_t s_staged[HA_FIELDS_MAX];
static int s_staged_count;
static bool s_drain_posted;

static const char *bat_state_name(uint8_t state)
{
    static const char *const k_names[] = { "unknown", "discharging", "charging", "full" };
    return state < sizeof(k_names) / sizeof(k_names[0]) ? k_names[state] : "unknown";
}

static void device_id(char *out, size_t size)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA); /* spec §5.5 */
    snprintf(out, size, "reflbo-%02x%02x", mac[4], mac[5]);
}

bool app_mqtt_on(void)
{
    const settings_t *s = app_settings();
    return s->mqtt_enabled && s->mqtt_host[0] != '\0';
}

/* The connection's settings, with the password from NVS `secrets` (spec §12.1, §14.2): NVS is up, as Wi-Fi is. */
static void make_conn(ha_conn_t *c)
{
    const settings_t *s = app_settings();
    memset(c, 0, sizeof(*c));
    device_id(c->id, sizeof(c->id));
    snprintf(c->host, sizeof(c->host), "%s", s->mqtt_host);
    c->port = s->mqtt_port;
    snprintf(c->user, sizeof(c->user), "%s", s->mqtt_user);
    c->discovery = s->mqtt_discovery;
    app_secret_get(SETTINGS_SECRET_MQTT_PASS, c->password, sizeof(c->password));
}

/* What HA's sensors expire by (spec §12.3): the sync's interval, or in sync mode `always` 10 min and the
 * longest span Wi-Fi is off, quiet hours or a night of the schedule. */
static uint32_t expected_s(void)
{
    const settings_t *s = app_settings();
    sync_schedule_t q = { .quiet = s->quiet, .quiet_from = s->quiet_from, .quiet_to = s->quiet_to };
    uint32_t quiet = sync_quiet_span_s(&q), night = ui_schedule_longest_night_s(&app_presets()->schedule);
    return ha_expected_s(s->sync_mode == SETTINGS_SYNC_ALWAYS, app_sync_expected_s(), quiet > night ? quiet : night);
}

static bool parse_fields(const char *text, void *ctx)
{
    char err[96];
    if (!ha_fields_from_json(text, ctx, err, sizeof(err))) {
        ESP_LOGW(TAG, "%s: %s", STORAGE_MQTT_FIELDS_PATH, err);
        return false;
    }
    return true;
}

/* The mappings in /fs/cfg/mqtt_fields.json (spec §12.5), once a boot; none if it is missing or invalid. */
static void load_fields(void)
{
    if (s_fields_loaded) {
        return;
    }
    s_fields_loaded = true;
    bool from_backup = false;
    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
    if (err == ESP_OK) {
        err = storage_load(STORAGE_MQTT_FIELDS_PATH, s_fields_json, sizeof(s_fields_json), parse_fields, &s_fields,
                           &from_backup);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%d MQTT fields%s", s_fields.count, from_backup ? ", from the backup" : "");
    } else {
        memset(&s_fields, 0, sizeof(s_fields)); /* a failed parse may have left it half-written */
        if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "MQTT fields: %s; none", esp_err_to_name(err));
        }
    }
}

void app_mqtt_boot(bool warm)
{
    if (warm && ha_store_valid(&s_store)) {
        return; /* a routine wake reads no file: the mappings load when a sync or the console needs them */
    }
    if (warm) {
        ESP_LOGW(TAG, "the MQTT values' RTC block is invalid; starting it again");
    }
    ha_store_init(&s_store);
    load_fields();
    ha_store_rebuild(&s_store, &s_fields);
}

void app_mqtt_seal(void)
{
    ha_store_seal(&s_store);
}

void app_mqtt_clock_moved(int64_t delta_s)
{
    ha_store_shift_time(&s_store, delta_s); /* values that came before a sync set the clock keep their age */
}

const ha_store_t *app_mqtt_store(void)
{
    uint32_t expected = expected_s(); /* spec §12.5: twice the expected interval; never stale in manual mode */
    ha_store_set_default_ttl(&s_store, expected <= UINT32_MAX / 2 ? expected * 2 : UINT32_MAX);
    return &s_store;
}

/* The state (spec §12.2) as it is now; the strings point into the app's state. */
static void build_state(ha_state_t *out)
{
    const app_ui_state_t *st = app_state();
    *out = (ha_state_t){ .charging = "unknown", .fw = esp_app_get_description()->version,
                         .uptime_s = (uint32_t)(app_uptime_ms() / 1000) };
    ds_entry_t e;
    if (ds_get(app_ds(), DS_ENV_TEMP, &e) && e.updated != 0) {
        out->has_temp = true;
        out->temp_c100 = e.value;
    }
    if (ds_get(app_ds(), DS_ENV_HUM, &e) && e.updated != 0) {
        out->has_hum = true;
        out->hum_pct100 = e.value;
    }
    if (ds_get(app_ds(), DS_BAT_LEVEL, &e) && e.updated != 0) {
        out->has_battery = true;
        out->bat_pct = e.value;
        out->bat_mv = e.mv;
        out->charging = bat_state_name(e.bat_state);
    }
    if (app_net_ready()) {
        netmgr_status_t ns;
        netmgr_status(&ns);
        if (ns.state == NETMGR_STATION) {
            out->has_rssi = true;
            out->rssi = ns.rssi;
        }
    }
    const ui_preset_t *p = &st->presets.presets[st->presets.active];
    out->preset_id = p->id;
    out->preset_name = p->name;
    if (timekeeping_valid()) { /* a sync that reached its MQTT step is the last one (spec §9.3) */
        out->last_sync = app_sync_running() ? (uint32_t)time(NULL) : st->sync.last_ok_at;
    }
}

/* On the app task, for the client's task (ha_hooks_t.payloads): the state and every discovery config. */
static void fill_payloads(void *arg)
{
    ha_payloads_t *out = arg;
    ha_state_t state;
    build_state(&state);
    ha_state_json(&state, out->state, sizeof(out->state));
    const ui_presets_t *p = app_presets();
    const char *names[UI_PRESET_MAX];
    for (int i = 0; i < p->count; i++) {
        names[i] = p->presets[i].name;
    }
    char id[16], broker[SETTINGS_HOST_LEN + 8];
    device_id(id, sizeof(id));
    snprintf(broker, sizeof(broker), "%s:%u", app_settings()->mqtt_host, app_settings()->mqtt_port);
    ha_disc_t d = { .id = id, .prefix = app_settings()->mqtt_prefix, .fw = esp_app_get_description()->version,
                    .expire_after_s = ha_expire_after_s(expected_s()), .presets = names, .preset_count = p->count,
                    .broker = broker };
    for (int i = 0; i < HA_DISC_COUNT; i++) {
        if (!ha_disc_message(&d, i, out->topic[i], sizeof(out->topic[i]), out->payload[i], sizeof(out->payload[i]))) {
            ESP_LOGE(TAG, "discovery config %d doesn't fit: left out", i);
            out->topic[i][0] = '\0';
        }
    }
    out->disc_hash = ha_disc_hash(&d);
}

static bool on_payloads(ha_payloads_t *out)
{
    return app_execute(fill_payloads, out) == ESP_OK;
}

typedef struct {
    ha_cmd_t cmd;
    bool press;                /* the payload is a button's PRESS */
    bool fits;                 /* the whole payload is in `text` */
    char text[HA_MESSAGE_LEN]; /* a preset's id or name, or the message */
} command_t;

/* spec §12.4: on the app task, in the order the commands came. */
static void apply_command(void *arg)
{
    command_t *c = arg;
    const ui_presets_t *p = app_presets();
    bool always = app_settings()->sync_mode == SETTINGS_SYNC_ALWAYS;
    if ((c->cmd == HA_CMD_NEXT || c->cmd == HA_CMD_SYNC) && !c->press) {
        ESP_LOGW(TAG, "command %d: \"%s\" isn't PRESS; ignored", c->cmd, c->text);
    } else if (c->cmd == HA_CMD_PRESET) {
        int i = c->fits ? ui_presets_lookup(p, c->text) : -1;
        if (i < 0) {
            ESP_LOGW(TAG, "cmd/preset: no preset \"%s\"; ignored", c->text);
        } else {
            ESP_LOGI(TAG, "cmd/preset: %s", p->presets[i].id);
            app_ui_select(i, true); /* saved like a manual switch */
            s_state_now = true;
        }
    } else if (c->cmd == HA_CMD_NEXT) {
        app_ui_select(ui_presets_next(p, always), true); /* as KEY short (D32) */
        ESP_LOGI(TAG, "cmd/next: %s", p->presets[p->active].id);
        s_state_now = true;
    } else if (c->cmd == HA_CMD_SYNC) {
        if (app_sync_active()) { /* one that waited while the board slept comes in a sync (D32) */
            ESP_LOGI(TAG, "cmd/sync: a sync runs; ignored");
        } else if (!always) {
            ESP_LOGI(TAG, "cmd/sync: only in sync mode always; ignored");
        } else {
            esp_err_t err = app_sync_now();
            ESP_LOGI(TAG, "cmd/sync: %s", err == ESP_OK ? "a sync starts" : esp_err_to_name(err));
        }
    } else if (c->cmd == HA_CMD_MESSAGE) {
        ha_store_set_message(&s_store, c->text, time(NULL));
        ESP_LOGI(TAG, "cmd/message: %s", c->text[0] != '\0' ? c->text : "cleared");
        app_ui_render();
    }
    if (s_state_now && s_keeping) {
        s_check_ms = 0; /* sync mode `always`: the next tick publishes it; a sync's session does at its end */
    }
    free(c);
}

static void on_command(ha_cmd_t cmd, const char *payload, size_t len)
{
    command_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        ESP_LOGW(TAG, "no memory: command %d dropped", cmd);
        return;
    }
    c->cmd = cmd;
    c->press = ha_cmd_press(payload, len);
    if (cmd == HA_CMD_MESSAGE) {
        ha_message_text(payload, len, c->text, sizeof(c->text)); /* cut at a character (spec §12.7) */
        c->fits = true;
    } else {
        c->fits = len < sizeof(c->text);
        snprintf(c->text, sizeof(c->text), "%.*s", (int)(c->fits ? len : sizeof(c->text) - 1), payload);
    }
    if (app_post(apply_command, c) != ESP_OK) {
        ESP_LOGW(TAG, "the app is busy: command %d dropped", cmd);
        free(c);
    }
}

/* On the app task: the values that came since the last time, then one render if any shows differently. */
static void drain_values(void *arg)
{
    (void)arg;
    EXT_RAM_BSS_ATTR static staged_t batch[HA_FIELDS_MAX];
    xSemaphoreTake(s_stage_lock, portMAX_DELAY);
    int n = s_staged_count;
    memcpy(batch, s_staged, (size_t)n * sizeof(batch[0]));
    s_staged_count = 0;
    s_drain_posted = false;
    xSemaphoreGive(s_stage_lock);
    time_t now = time(NULL);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        changed |= ha_store_set(&s_store, ha_store_find(&s_store, batch[i].key), &batch[i].v, now);
    }
    if (changed) {
        app_ui_render();
    }
}

static void on_value(const char *key, const ha_value_t *v)
{
    xSemaphoreTake(s_stage_lock, portMAX_DELAY);
    int i = 0;
    while (i < s_staged_count && strcmp(s_staged[i].key, key) != 0) {
        i++;
    }
    if (i == s_staged_count && i < HA_FIELDS_MAX) {
        snprintf(s_staged[i].key, sizeof(s_staged[i].key), "%s", key);
        s_staged_count++;
    }
    if (i < HA_FIELDS_MAX) {
        s_staged[i].v = *v; /* the latest one wins */
    }
    bool post = !s_drain_posted;
    s_drain_posted = true;
    xSemaphoreGive(s_stage_lock);
    if (post && app_post(drain_values, NULL) != ESP_OK) {
        xSemaphoreTake(s_stage_lock, portMAX_DELAY);
        s_drain_posted = false; /* the next value tries again */
        xSemaphoreGive(s_stage_lock);
        ESP_LOGW(TAG, "the app is busy: the values wait");
    }
}

static void status_changed(void *arg)
{
    (void)arg;
    app_ui_render(); /* the status bar's mark (spec §5.2) */
}

static void on_status(void)
{
    app_post(status_changed, NULL);
}

/* The client's task, once, and the mappings it subscribes to, once a boot. */
static void start(void)
{
    if (!s_started) {
        static const ha_hooks_t k_hooks = { .command = on_command, .value = on_value, .status = on_status,
                                            .payloads = on_payloads };
        s_stage_lock = s_stage_lock != NULL ? s_stage_lock : xSemaphoreCreateMutex();
        esp_err_t err = s_stage_lock != NULL ? ha_mqtt_init(&k_hooks) : ESP_ERR_NO_MEM;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "the client didn't start: %s", esp_err_to_name(err));
            return;
        }
        s_started = true;
    }
    if (!s_fields_sent) {
        load_fields();
        ha_mqtt_set_fields(&s_fields);
        s_fields_sent = true;
    }
}

void app_mqtt_prepare(void)
{
    start();
    make_conn(&s_conn);
}

esp_err_t app_mqtt_sync_step(int budget_ms, char *detail, size_t size) /* on the sync's task */
{
    if (!s_started) {
        snprintf(detail, size, "no client");
        return ESP_FAIL;
    }
    return ha_mqtt_session(&s_conn, budget_ms, detail, size);
}

/* Sync mode `always` (spec §12.9): connected while it keeps Wi-Fi on the network; the state on a change, at
 * most every 30 s, at once after a command, and every 5 min, with the retained values again. */
void app_mqtt_tick(void)
{
    bool want = app_mqtt_on() && app_sync_lan_ui();
    if (want && !s_keeping) {
        start();
        if (!s_started) {
            return; /* no memory for the client: the next tick tries again */
        }
        ha_conn_t c;
        make_conn(&c);
        ha_mqtt_keep(&c);
        s_keeping = true;
        s_published_ms = -1;
        s_state_now = false;
        s_resubscribe_ms = app_uptime_ms() + RESUBSCRIBE_MS; /* connecting subscribes */
        ESP_LOGI(TAG, "sync mode always: connecting to %s", c.host);
    } else if (!want && s_keeping) {
        ha_mqtt_drop();
        s_keeping = false;
        ESP_LOGI(TAG, "sync mode always: disconnected");
    }
    int64_t now = app_uptime_ms();
    if (!s_keeping || now < s_check_ms) {
        return;
    }
    s_check_ms = now + CHECK_MS;
    ha_mqtt_status_t hs;
    ha_mqtt_status(&hs);
    if (!hs.connected) {
        return;
    }
    if (now >= s_resubscribe_ms) {
        ha_mqtt_resubscribe(); /* a value that doesn't change stays fresh */
        s_resubscribe_ms = now + RESUBSCRIBE_MS;
    }
    ha_state_t state;
    build_state(&state);
    EXT_RAM_BSS_ATTR static char json[HA_STATE_MAX], same[HA_STATE_MAX];
    ha_state_t still = state;
    still.uptime_s = 0;
    still.has_rssi = false; /* the signal jitters: it goes out with the 5-min state, not as a change */
    ha_state_json(&still, same, sizeof(same));
    bool changed = strcmp(same, s_published) != 0;
    if (s_state_now || ha_state_due(changed, s_published_ms < 0 ? -1 : (now - s_published_ms) / 1000)) {
        ha_state_json(&state, json, sizeof(json));
        ha_mqtt_publish_state(json);
        memcpy(s_published, same, sizeof(s_published));
        s_published_ms = now;
        s_state_now = false;
    }
}

int64_t app_mqtt_deadline_ms(void)
{
    return s_keeping ? s_check_ms : 0;
}

void app_mqtt_settings_changed(const settings_t *before)
{
    const settings_t *s = app_settings();
    if (before->mqtt_discovery && !s->mqtt_discovery) {
        ha_mqtt_forget_discovery(); /* on again, it goes out again (spec §12.3) */
    }
    bool moved = before->mqtt_enabled != s->mqtt_enabled || strcmp(before->mqtt_host, s->mqtt_host) != 0 ||
                 before->mqtt_port != s->mqtt_port || strcmp(before->mqtt_user, s->mqtt_user) != 0 ||
                 before->mqtt_discovery != s->mqtt_discovery || strcmp(before->mqtt_prefix, s->mqtt_prefix) != 0;
    if (moved && s_keeping) {
        ha_mqtt_drop(); /* the next tick connects with the new settings */
        s_keeping = false;
    }
}

void app_mqtt_password_changed(void)
{
    if (s_keeping) {
        ha_mqtt_drop();
        s_keeping = false;
    }
}

/* spec §12.8: while connected, a dashboard gesture goes to HA too; at other times nothing is sent. */
void app_mqtt_key(board_button_t button, gesture_t gesture)
{
    if (!s_started || gesture < GESTURE_SHORT || gesture > GESTURE_LONG) {
        return;
    }
    ha_press_t press = gesture == GESTURE_SHORT ? HA_PRESS_SHORT : gesture == GESTURE_DOUBLE ? HA_PRESS_DOUBLE
                                                                                          : HA_PRESS_LONG;
    ha_mqtt_publish_action(ha_action_payload(button == BOARD_BUTTON_BOOT, press));
}

bool app_mqtt_banner(void)
{
    return ha_store_banner(&s_store, time(NULL));
}

void app_mqtt_dismiss(void)
{
    ha_store_dismiss(&s_store);
    ESP_LOGI(TAG, "KEY short: the message's banner dismissed");
}

void app_mqtt_set_message(const char *text)
{
    char clean[HA_MESSAGE_LEN];
    ha_message_text(text, strlen(text), clean, sizeof(clean));
    ha_store_set_message(&s_store, clean, time(NULL));
}

/* As a payload would bring it at the mapping's path (spec §15): the console's `field set mqtt.<key>`. */
bool app_mqtt_set_value(const char *key, const char *text, char *err, size_t size)
{
    load_fields();
    int m = ha_fields_find(&s_fields, key), i = ha_store_find(&s_store, key);
    if (m < 0 || i < 0) {
        snprintf(err, size, "no MQTT field \"%s\" (see `mqtt status`)", key);
        return false;
    }
    ha_field_t f = s_fields.field[m];
    f.json_path[0] = '\0';
    ha_value_t v;
    if (!ha_value_parse(&f, text, strlen(text), &v)) {
        snprintf(err, size, "mqtt.%s takes a %s", key, ha_kind_name(f.kind));
        return false;
    }
    ha_store_set(&s_store, i, &v, time(NULL));
    return true;
}

bool app_mqtt_clear_value(const char *key)
{
    return ha_store_clear(&s_store, ha_store_find(&s_store, key));
}

void app_mqtt_status(app_mqtt_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->on = app_mqtt_on();
    out->keeping = s_keeping;
    out->password_set = app_secret_set(SETTINGS_SECRET_MQTT_PASS);
    ha_mqtt_status(&out->client);
}

size_t app_mqtt_fields_json(char *out, size_t size)
{
    load_fields();
    return ha_fields_to_json(&s_fields, out, size);
}

/* PUT /api/mqtt_fields, a restore (spec §12.5): saved, then the values, the client and the screen follow. */
esp_err_t app_mqtt_replace_fields(const ha_fields_t *f)
{
    size_t n = ha_fields_to_json(f, s_fields_json, sizeof(s_fields_json));
    esp_err_t err = n == 0 ? ESP_ERR_INVALID_SIZE : storage_init();
    if (err == ESP_OK) {
        err = storage_write_atomic(STORAGE_MQTT_FIELDS_PATH, s_fields_json, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the MQTT fields: %s", esp_err_to_name(err));
        return err;
    }
    if (f != &s_fields) {
        s_fields = *f;
    }
    s_fields_loaded = true;
    ha_store_rebuild(&s_store, &s_fields); /* a key with the same kind keeps its value */
    s_fields_sent = s_started;
    if (s_started) {
        ha_mqtt_set_fields(&s_fields); /* a kept connection subscribes to them at once */
    }
    ESP_LOGI(TAG, "%d MQTT fields saved", s_fields.count);
    app_ui_render();
    return ESP_OK;
}

/* POST /api/mqtt/test (spec §10.3): the saved broker, from the owner's network. */
esp_err_t app_mqtt_test(void)
{
    if (app_settings()->mqtt_host[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    netmgr_status_t ns;
    netmgr_status(&ns);
    if (ns.state != NETMGR_STATION || ns.ip[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    start();
    if (!s_started) {
        return ESP_FAIL;
    }
    ha_conn_t c;
    make_conn(&c);
    ha_mqtt_test(&c);
    return ESP_OK;
}

bool app_mqtt_failed(void)
{
    if (!app_mqtt_on()) {
        return false;
    }
    if (s_keeping) {
        ha_mqtt_status_t hs;
        ha_mqtt_status(&hs);
        return !hs.connected && hs.detail[0] != '\0'; /* down after a failure, not while connecting */
    }
    return app_state()->sync.last_at != 0 && app_state()->sync.last_result[SYNC_STEP_MQTT] == SYNC_STEP_FAILED;
}

void app_mqtt_summary(char *out, size_t size)
{
    const lang_t *lang = lang_get(app_settings()->language);
    const app_sync_state_t *st = &app_state()->sync;
    if (!app_mqtt_on()) {
        snprintf(out, size, "%s", lang_str(lang, LS_OFF));
        return;
    }
    if (s_keeping) {
        ha_mqtt_status_t hs;
        ha_mqtt_status(&hs);
        snprintf(out, size, "%s",
                 hs.connected ? lang_str(lang, LS_MQTT_CONNECTED) : hs.detail[0] ? hs.detail : "\xE2\x80\xA6");
        return;
    }
    uint8_t r = st->last_at != 0 ? st->last_result[SYNC_STEP_MQTT] : SYNC_STEP_NOT_RUN;
    if (r == SYNC_STEP_NOT_RUN) {
        snprintf(out, size, "%s", lang_str(lang, LS_SYNC_NEVER));
        return;
    }
    time_t at = st->last_at;
    struct tm local;
    localtime_r(&at, &local);
    char when[12];
    const char *suffix;
    lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
    snprintf(out, size, "%s%s%s %s", when, suffix[0] ? " " : "", suffix,
             r == SYNC_STEP_OK ? "OK" : st->last_detail[SYNC_STEP_MQTT]);
}

/* `mqtt status` (spec §15): the settings, the client, the last session, the mappings and the message. */
void app_mqtt_print_status(void)
{
    const settings_t *s = app_settings();
    printf("mqtt %s, broker %s:%u, user \"%s\", password %s, discovery %s (prefix %s)\n",
           s->mqtt_enabled ? "on" : "off", s->mqtt_host[0] ? s->mqtt_host : "-", s->mqtt_port, s->mqtt_user,
           app_secret_set(SETTINGS_SECRET_MQTT_PASS) ? "set" : "none", s->mqtt_discovery ? "on" : "off",
           s->mqtt_prefix);
    ha_mqtt_status_t hs;
    ha_mqtt_status(&hs);
    char summary[48];
    app_mqtt_summary(summary, sizeof(summary));
    printf("client: %s, %s%s%s; last: %s\n",
           !s_started  ? "not started"
           : s_keeping ? "kept connected (sync mode always)"
                       : "a session in each sync",
           hs.connected ? "connected" : "not connected", hs.detail[0] ? ": " : "", hs.detail, summary);
    if (hs.test_running || hs.test_done) {
        printf("test: %s%s\n", hs.test_running ? "running" : hs.test_ok ? "connected" : "failed: ",
               hs.test_running || hs.test_ok ? "" : hs.test_detail);
    }
    load_fields();
    const ha_store_t *st = app_mqtt_store();
    printf("%d of %d fields mapped; values stale by default after %lu s%s\n", s_fields.count, HA_FIELDS_MAX,
           (unsigned long)st->default_ttl_s, st->default_ttl_s == 0 ? " (never)" : "");
    time_t now = time(NULL);
    for (int i = 0; i < s_fields.count; i++) {
        const ha_field_t *f = &s_fields.field[i];
        printf("  mqtt.%-23s %-6s %s%s%s: ", f->key, ha_kind_name(f->kind), f->topic, f->json_path[0] ? " at " : "",
               f->json_path);
        int k = ha_store_find(st, f->key);
        if (k < 0 || st->entry[k].updated == 0) {
            printf("no value\n");
            continue;
        }
        const ha_entry_t *e = &st->entry[k];
        char value[HA_TEXT_LEN];
        if (e->none) { /* D40: HA's unknown or unavailable, or an empty payload */
            snprintf(value, sizeof(value), "none");
        } else if (e->kind == HA_KIND_NUMBER) {
            lang_format_decimal(lang_get("en"), e->number, e->decimals, value, sizeof(value));
        } else if (e->kind == HA_KIND_TIME) {
            time_t t = (time_t)e->time;
            struct tm local;
            localtime_r(&t, &local);
            strftime(value, sizeof(value), "%Y-%m-%d %H:%M", &local);
        } else {
            snprintf(value, sizeof(value), "%s", e->text); /* a state's label already (D40) */
        }
        printf("%s%s%s, %lu s old%s\n", value, e->unit[0] ? " " : "", e->unit,
               (unsigned long)(now > (time_t)e->updated ? now - (time_t)e->updated : 0),
               ha_store_freshness(st, k, now) == HA_STALE ? ", stale" : "");
    }
    uint32_t disc = ha_mqtt_discovery_hash();
    printf("discovery: %s", disc != 0 ? "sent, hash " : "not sent yet\n");
    if (disc != 0) {
        printf("%08lx\n", (unsigned long)disc);
    }
    if (st->message_at == 0) {
        printf("message: none\n");
    } else {
        printf("message: \"%s\", %lu s old, banner %s\n", st->message,
               (unsigned long)(now > (time_t)st->message_at ? now - (time_t)st->message_at : 0),
               ha_store_banner(st, now) ? "shown" : st->message_dismissed ? "dismissed" : "over");
    }
}
