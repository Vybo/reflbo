#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "app_internal.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "ha_mqtt.h"
#include "ha_session.h"
#include "lang.h"
#include "netmgr.h"
#include "nvs.h"
#include "sync_plan.h"
#include "timekeeping.h"

/* MQTT and Home Assistant on the app's side (spec §12, D32): the client's hooks, the sync's step, sync mode
 * `always`'s connection and its state, and what the status bar and Info say about them. It belongs to the
 * app task, but for the hooks, which run on the client's and the sync's tasks and hand over to it. */

static const char *TAG = "app_mqtt";

#define NVS_KEY_PASS "mqtt_pass" /* in `secrets`: write-only (spec §12.1) */
#define CHECK_MS 30000           /* sync mode `always`: how often the state is looked at (spec §12.9) */

static bool s_started;           /* the client's task runs */
static ha_conn_t s_conn;         /* the next session's: set on the app task as a sync starts */
static bool s_keeping;           /* sync mode `always` keeps the client */
static int64_t s_published_ms = -1;
static int64_t s_check_ms;
EXT_RAM_BSS_ATTR static char s_published[HA_STATE_MAX]; /* the last state that went out, its uptime left out */

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

/* The connection's settings, with the password from NVS (`secrets`, spec §12.1): NVS is up, as Wi-Fi is. */
static void make_conn(ha_conn_t *c)
{
    const settings_t *s = app_settings();
    memset(c, 0, sizeof(*c));
    device_id(c->id, sizeof(c->id));
    snprintf(c->host, sizeof(c->host), "%s", s->mqtt_host);
    c->port = s->mqtt_port;
    snprintf(c->user, sizeof(c->user), "%s", s->mqtt_user);
    c->discovery = s->mqtt_discovery;
    nvs_handle_t nvs;
    if (nvs_open("secrets", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(c->password);
        if (nvs_get_str(nvs, NVS_KEY_PASS, c->password, &len) != ESP_OK) {
            c->password[0] = '\0';
        }
        nvs_close(nvs);
    }
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

static void on_command(ha_cmd_t cmd, const char *payload, size_t len)
{
    ESP_LOGI(TAG, "command %d (%.*s): applied from M7's next step", cmd, (int)len, payload);
}

static void on_value(const char *key, const ha_value_t *v)
{
    (void)v;
    ESP_LOGD(TAG, "value for %s", key);
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

static void start(void)
{
    if (s_started) {
        return;
    }
    static const ha_hooks_t k_hooks = { .command = on_command, .value = on_value, .status = on_status,
                                        .payloads = on_payloads };
    esp_err_t err = ha_mqtt_init(&k_hooks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "the client didn't start: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
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
 * most every 30 s, and every 5 min. */
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
    ha_state_t state;
    build_state(&state);
    EXT_RAM_BSS_ATTR static char json[HA_STATE_MAX], same[HA_STATE_MAX];
    ha_state_t still = state;
    still.uptime_s = 0;
    still.has_rssi = false; /* the signal jitters: it goes out with the 5-min state, not as a change */
    ha_state_json(&still, same, sizeof(same));
    bool changed = strcmp(same, s_published) != 0;
    if (ha_state_due(changed, s_published_ms < 0 ? -1 : (now - s_published_ms) / 1000)) {
        ha_state_json(&state, json, sizeof(json));
        ha_mqtt_publish_state(json);
        memcpy(s_published, same, sizeof(s_published));
        s_published_ms = now;
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
    snprintf(out, size, "%s%s%s %s", when, suffix[0] ? " " : "", suffix, r == SYNC_STEP_OK ? "OK" : st->mqtt_detail);
}
