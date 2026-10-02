#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "cJSON.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "netmgr.h"
#include "sensors.h"
#include "storage_backup.h"
#include "timekeeping.h"
#include "ui_catalog.h"
#include "ui_dashboard.h"
#include "webui.h"

/* The web configurator's API (spec §10.3), apart from what webui answers itself (the password,
 * Wi-Fi, OTA and the system actions). Every call runs on the app task, which owns this state. */

static const char *TAG = "app_web";

#define MIN_EPOCH 1577836800 /* 2020-01-01: a phone's clock is at least this */
#define MAX_EPOCH              4102444799 /* 2099-12-31 23:59:59: the RTC's last second */

static void reply_error(webui_reply_t *reply, uint8_t *out, size_t size, int status, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    reply->status = cJSON_PrintPreallocated(o, (char *)out, (int)size, false) ? status : 500;
    reply->len = reply->status == status ? strlen((char *)out) : 0;
    reply->type = "application/json";
    cJSON_Delete(o);
}

/* Prints `o` into the reply and frees it. */
static void reply_cjson(webui_reply_t *reply, uint8_t *out, size_t size, cJSON *o)
{
    if (cJSON_PrintPreallocated(o, (char *)out, (int)size, false)) {
        reply->status = 200;
        reply->len = strlen((char *)out);
        reply->type = "application/json";
    } else {
        reply_error(reply, out, size, 500, "the reply doesn't fit");
    }
    cJSON_Delete(o);
}

/* A JSON text written by one of the codecs; 0 bytes means it didn't fit. */
static void reply_text(webui_reply_t *reply, uint8_t *out, size_t size, size_t len)
{
    if (len == 0) {
        reply_error(reply, out, size, 500, "the reply doesn't fit");
        return;
    }
    reply->status = 200;
    reply->len = len;
    reply->type = "application/json";
}

static const char *battery_state_name(uint8_t state)
{
    static const char *const k_names[] = { "unknown", "discharging", "charging", "full" };
    return state < sizeof(k_names) / sizeof(k_names[0]) ? k_names[state] : "unknown";
}

static const char *net_state_name(netmgr_state_t state)
{
    switch (state) {
    case NETMGR_JOINING:
        return "joining";
    case NETMGR_STATION:
        return "station";
    case NETMGR_AP:
        return "ap";
    default:
        return "off";
    }
}

/* A datastore value in its unit (0.01 °C, 0.01 %, 0.1 d) as a JSON number, or null. */
static void add_value(cJSON *o, const char *key, ds_field_t field, double scale)
{
    ds_entry_t e;
    if (ds_get(app_ds(), field, &e) && e.updated != 0) {
        cJSON_AddNumberToObject(o, key, e.value / scale);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
{
    const app_ui_state_t *st = app_state();
    const esp_app_desc_t *app = esp_app_get_description();
    netmgr_status_t net;
    netmgr_status(&net);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char text[40];
    cJSON *o = cJSON_CreateObject();

    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON_AddStringToObject(dev, "id", net.host);
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(dev, "mac", text);
    cJSON_AddStringToObject(dev, "firmware", app->version);
    cJSON_AddNumberToObject(dev, "uptime_s", (double)(app_uptime_ms() / 1000));
    cJSON_AddNumberToObject(dev, "free_heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(dev, "language", st->settings.language);

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    cJSON *t = cJSON_AddObjectToObject(o, "time");
    cJSON_AddBoolToObject(t, "valid", timekeeping_valid());
    cJSON_AddNumberToObject(t, "epoch", (double)now);
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
    cJSON_AddStringToObject(t, "local", text);
    cJSON_AddStringToObject(t, "tz_iana", st->settings.tz_iana);
    cJSON_AddStringToObject(t, "tz_posix", st->settings.tz_posix);

    cJSON *bat = cJSON_AddObjectToObject(o, "battery");
    ds_entry_t e;
    if (ds_get(app_ds(), DS_BAT_LEVEL, &e) && e.updated != 0) {
        cJSON_AddNumberToObject(bat, "percent", e.value);
        cJSON_AddNumberToObject(bat, "mv", e.mv);
        cJSON_AddStringToObject(bat, "state", battery_state_name(e.bat_state));
    }
    add_value(bat, "days_left", DS_BAT_DAYS, 10.0);
    cJSON_AddBoolToObject(bat, "critical", st->critical);
    const battery_learn_t *l = sensors_learn(); /* D21 */
    cJSON *learn = cJSON_AddObjectToObject(bat, "learn");
    cJSON_AddStringToObject(learn, "state", battery_learn_state_name(battery_learn_state(l)));
    cJSON_AddNumberToObject(learn, "hours", battery_learn_hours(l));

    cJSON *env = cJSON_AddObjectToObject(o, "sensors");
    add_value(env, "temp_c", DS_ENV_TEMP, 100.0);
    add_value(env, "hum_pct", DS_ENV_HUM, 100.0);
    if (ds_get(app_ds(), DS_ENV_TEMP, &e) && e.updated != 0 && now >= (time_t)e.updated) {
        cJSON_AddNumberToObject(env, "age_s", (double)(now - (time_t)e.updated));
    }

    cJSON *wifi = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(wifi, "state", net_state_name(net.state));
    cJSON_AddStringToObject(wifi, "ssid", net.ssid);
    cJSON_AddStringToObject(wifi, "ip", net.ip);
    cJSON_AddNumberToObject(wifi, "rssi", net.rssi);
    cJSON_AddBoolToObject(wifi, "ap_on", net.ap_on);
    cJSON_AddStringToObject(wifi, "ap_ssid", net.ap_ssid);
    cJSON_AddNumberToObject(wifi, "ap_clients", net.ap_clients);

    /* spec §10.3, M5: the sync, the RTC's trim and the forecast's age */
    cJSON *sync = cJSON_AddObjectToObject(o, "sync");
    static const char *const k_modes[] = { "times", "interval", "always", "manual" };
    cJSON_AddStringToObject(sync, "mode", k_modes[st->settings.sync_mode <= SETTINGS_SYNC_MANUAL
                                                       ? st->settings.sync_mode : 0]);
    bool running = app_sync_running(); /* a radar refresh isn't a sync (spec §9.3) */
    cJSON_AddBoolToObject(sync, "running", running);
    if (running) {
        cJSON_AddStringToObject(sync, "step", sync_step_name(sync_step()));
    }
    if (st->sync.last_at != 0) {
        cJSON *last = cJSON_AddObjectToObject(sync, "last");
        cJSON_AddNumberToObject(last, "at", st->sync.last_at);
        cJSON *steps = cJSON_AddObjectToObject(last, "steps");
        static const char *const k_results[] = { "skipped", "ok", "failed" };
        for (int i = 0; i < SYNC_STEP_COUNT; i++) {
            uint8_t r = st->sync.last_result[i];
            cJSON_AddStringToObject(steps, sync_step_name((sync_step_t)i), k_results[r <= SYNC_STEP_FAILED ? r : 0]);
        }
        if (st->sync.last_failed_step < SYNC_STEP_COUNT) {
            cJSON_AddStringToObject(last, "failed", sync_step_name((sync_step_t)st->sync.last_failed_step));
            cJSON_AddStringToObject(last, "detail", st->sync.last_detail);
        }
    }
    if (st->sync.due.at != 0) {
        cJSON_AddNumberToObject(sync, "next", (double)st->sync.due.at);
        cJSON_AddBoolToObject(sync, "next_retry", st->sync.due.retry);
    }
    const ds_weather_t *w = ds_weather(app_ds());
    if (w != NULL) {
        cJSON_AddNumberToObject(sync, "weather_at", w->fetched);
    }
    const ds_air_t *a = ds_air(app_ds());
    if (a != NULL) {
        cJSON_AddNumberToObject(sync, "air_at", a->fetched);
    }
    const rtc_trim_t *trim = timekeeping_trim();
    cJSON *rtc = cJSON_AddObjectToObject(t, "rtc");
    cJSON_AddNumberToObject(rtc, "trim_steps", trim->offset);
    if (trim->drift_ppb != TRIM_NO_DRIFT) {
        cJSON_AddNumberToObject(rtc, "drift_s_per_day", timekeeping_trim_drift_s10_per_day(trim) / 10.0);
    }

    /* spec §10.3, M6: the weather radar's frames and the flight radar's polls */
    app_radar_status_t rs;
    app_radar_status(&rs);
    cJSON *radar = cJSON_AddObjectToObject(o, "radar");
    cJSON *weather = cJSON_AddObjectToObject(radar, "weather");
    cJSON_AddStringToObject(weather, "source", rs.source == RADAR_SOURCE_CHMU ? "chmu" : "rainviewer");
    cJSON_AddNumberToObject(weather, "frames", rs.frames);
    if (rs.frame_at != 0) {
        cJSON_AddNumberToObject(weather, "frame_at", rs.frame_at);
    }
    if (rs.fetched_at != 0) {
        cJSON_AddNumberToObject(weather, "fetched_at", (double)rs.fetched_at);
        if (!rs.ok) {
            cJSON_AddStringToObject(weather, "error", rs.detail);
        }
    }
    app_flights_status_t fs;
    app_flights_status(&fs);
    cJSON *flights = cJSON_AddObjectToObject(radar, "flights");
    cJSON_AddBoolToObject(flights, "on", fs.on);
    cJSON_AddNumberToObject(flights, "aircraft", fs.aircraft);
    if (fs.updated != 0) {
        cJSON_AddNumberToObject(flights, "updated", (double)fs.updated);
    }
    cJSON_AddBoolToObject(flights, "failed", fs.failed);
    if (fs.failed && fs.error[0] != '\0') {
        cJSON_AddStringToObject(flights, "error", fs.error);
    }
    if (fs.routes_paused_until != 0) {
        cJSON_AddNumberToObject(flights, "routes_paused_until", (double)fs.routes_paused_until);
    }

    const ui_preset_t *active = &st->presets.presets[st->presets.active];
    cJSON *preset = cJSON_AddObjectToObject(o, "preset");
    cJSON_AddStringToObject(preset, "active", active->id);
    cJSON_AddStringToObject(preset, "name", active->name);
    cJSON_AddBoolToObject(preset, "cycle", st->presets.cycle_enabled);
    reply_cjson(reply, out, size, o);
}

/* The framebuffer the previews draw into; the display's own stays as it is. */
static gfx_fb_t *preview_fb(void)
{
    static gfx_fb_t fb;
    static uint8_t *buf;
    if (buf == NULL) {
        buf = heap_caps_malloc(gfx_fb_size(400, 300), MALLOC_CAP_SPIRAM);
        if (buf == NULL) {
            return NULL;
        }
        gfx_fb_init(&fb, buf, 400, 300);
    }
    return &fb;
}

static void reply_bmp(const gfx_fb_t *fb, uint8_t *out, size_t size, webui_reply_t *reply)
{
    size_t n = fb != NULL ? gfx_bmp_encode(fb, out, size) : 0;
    if (n == 0) {
        reply_error(reply, out, size, 500, "no image");
        return;
    }
    reply->status = 200;
    reply->len = n;
    reply->type = "image/bmp";
}

/* GET /api/preview.bmp?preset=<id>: a saved preset. POST: a presets document from the editor,
 * validated like presets.json, and ?preset=<id> in it (default: its active one). */
static void preview(const char *method, const char *query, const char *body, uint8_t *out, size_t size,
                    webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static ui_presets_t doc; /* scratch buffers in PSRAM (AGENTS.md §8) */
    char id[UI_PRESET_ID_LEN] = "", err[96];
    const char *eq = strstr(query, "preset=");
    if (eq != NULL) {
        snprintf(id, sizeof(id), "%.*s", (int)strcspn(eq + 7, "&"), eq + 7);
    }
    if (strcmp(method, "POST") == 0) {
        if (!ui_presets_from_json(body, &doc, err, sizeof(err))) {
            reply_error(reply, out, size, 400, err);
            return;
        }
    } else {
        doc = *app_presets();
    }
    int index = id[0] ? ui_presets_find(&doc, id) : doc.active;
    if (index < 0) {
        reply_error(reply, out, size, 404, "no such preset");
        return;
    }
    gfx_fb_t *fb = preview_fb();
    if (fb != NULL) {
        app_radar_prepare(&doc.presets[index]); /* its map, and the frame from its file if PSRAM has none */
        ui_context_t ctx;
        app_ui_context(&ctx);
        ui_draw_dashboard(fb, &ctx, &doc.presets[index]);
    }
    reply_bmp(fb, out, size, reply);
}

static bool json_number(const cJSON *o, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) {
        return false;
    }
    *out = v->valuedouble;
    return true;
}

/* POST /api/time (spec §10.3): {"epoch": <UTC seconds>, "tz_iana": ..., "tz_posix": ...}, the
 * zone optional. The app task notices the jump and schedules again (main/app.c). */
static void set_time(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
{
    cJSON *in = cJSON_Parse(body);
    double epoch = 0;
    const cJSON *iana = cJSON_GetObjectItemCaseSensitive(in, "tz_iana");
    const cJSON *posix = cJSON_GetObjectItemCaseSensitive(in, "tz_posix");
    bool zone = cJSON_IsString(iana) && cJSON_IsString(posix);
    if (!json_number(in, "epoch", &epoch) || epoch < MIN_EPOCH || epoch > MAX_EPOCH) {
        reply_error(reply, out, size, 400, "epoch: UTC seconds between 2020 and 2099");
    } else if (zone && (strlen(iana->valuestring) >= SETTINGS_TZ_IANA_LEN || iana->valuestring[0] == '\0' ||
                        strlen(posix->valuestring) >= SETTINGS_TZ_POSIX_LEN || posix->valuestring[0] == '\0')) {
        reply_error(reply, out, size, 400, "tz_iana and tz_posix: a zone name and its POSIX rule");
    } else if (timekeeping_set_utc((time_t)epoch) != ESP_OK) {
        reply_error(reply, out, size, 500, "the RTC didn't take the time");
    } else {
        if (zone) {
            app_ui_set_zone(iana->valuestring, posix->valuestring);
        }
        ESP_LOGI(TAG, "time set from the web UI%s", zone ? ", with the zone" : "");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        reply_cjson(reply, out, size, o);
    }
    cJSON_Delete(in);
}

/* POST /api/battery/learn (D21): {"start": true} arms learning from the next full discharge,
 * {"stop": true} ends it. */
static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
{
    cJSON *in = cJSON_Parse(body);
    bool start = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(in, "start"));
    bool stop = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(in, "stop"));
    cJSON_Delete(in);
    if (start == stop) {
        reply_error(reply, out, size, 400, "send {\"start\": true} or {\"stop\": true}");
        return;
    }
    if (start && !timekeeping_valid()) {
        reply_error(reply, out, size, 409, "set the clock first");
        return;
    }
    app_ui_learn(start);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", battery_learn_state_name(battery_learn_state(sensors_learn())));
    reply_cjson(reply, out, size, o);
}

/* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static char settings[2048];
    EXT_RAM_BSS_ATTR static char presets[UI_PRESETS_JSON_MAX];
    netmgr_status_t net;
    netmgr_status(&net);
    backup_file_t files[] = {
        { "settings.json", app_ui_settings_json(settings, sizeof(settings)) ? settings : NULL },
        { "presets.json", ui_presets_to_json(app_presets(), presets, sizeof(presets)) ? presets : NULL },
    };
    reply_text(reply, out, size,
               backup_build(files, 2, net.host, esp_app_get_description()->version, (char *)out, size));
}

/* POST /api/restore: every file is checked before any is replaced. */
static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static char buf[WEBUI_BODY_MAX];
    EXT_RAM_BSS_ATTR static ui_presets_t presets;
    backup_file_t files[8];
    char err[112];
    int n = backup_split(body, files, 8, buf, sizeof(buf), err, sizeof(err));
    if (n < 0) {
        reply_error(reply, out, size, 400, err);
        return;
    }
    const char *settings = NULL, *presets_text = NULL;
    cJSON *skipped = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].name, "settings.json") == 0) {
            settings = files[i].text;
        } else if (strcmp(files[i].name, "presets.json") == 0) {
            presets_text = files[i].text;
        } else {
            cJSON_AddItemToArray(skipped, cJSON_CreateString(files[i].name)); /* from a later firmware */
        }
    }
    char why[96];
    if (settings != NULL && !app_ui_check_settings(settings, why, sizeof(why))) {
        snprintf(err, sizeof(err), "settings.json: %s", why);
    } else if (presets_text != NULL && !ui_presets_from_json(presets_text, &presets, why, sizeof(why))) {
        snprintf(err, sizeof(err), "presets.json: %s", why);
    } else {
        err[0] = '\0';
    }
    if (err[0] != '\0') {
        cJSON_Delete(skipped);
        reply_error(reply, out, size, 400, err);
        return;
    }
    esp_err_t e = settings != NULL ? app_ui_replace_settings(settings, why, sizeof(why)) : ESP_OK;
    if (e == ESP_OK && presets_text != NULL) {
        e = app_ui_replace_presets(&presets);
    }
    if (e != ESP_OK) {
        cJSON_Delete(skipped);
        reply_error(reply, out, size, 500, "saving failed; some files may be restored");
        return;
    }
    ESP_LOGI(TAG, "restored %d file(s)", n - cJSON_GetArraySize(skipped));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddItemToObject(o, "skipped", skipped);
    reply_cjson(reply, out, size, o);
}

void app_web_api(const char *method, const char *path, const char *query, const char *body, uint8_t *out,
                 size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static ui_presets_t presets;
    char err[112];
    bool get = strcmp(method, "GET") == 0;
    if (strcmp(path, "/api/status") == 0 && get) {
        get_status(out, size, reply);
    } else if (strcmp(path, "/api/settings") == 0 && get) {
        reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
    } else if (strcmp(path, "/api/settings") == 0 && strcmp(method, "PATCH") == 0) {
        if (app_ui_patch_settings(body, err, sizeof(err)) != ESP_OK) {
            reply_error(reply, out, size, 400, err);
        } else {
            reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
        }
    } else if (strcmp(path, "/api/layouts") == 0 && get) {
        reply_text(reply, out, size, ui_catalog_layouts_json((char *)out, size));
    } else if (strcmp(path, "/api/fields") == 0 && get) {
        ui_context_t ctx;
        app_ui_context(&ctx);
        reply_text(reply, out, size, ui_catalog_fields_json(&ctx, (char *)out, size));
    } else if (strcmp(path, "/api/presets") == 0 && get) {
        reply_text(reply, out, size, ui_presets_to_json(app_presets(), (char *)out, size));
    } else if (strcmp(path, "/api/presets") == 0 && strcmp(method, "PUT") == 0) {
        if (!ui_presets_from_json(body, &presets, err, sizeof(err))) {
            reply_error(reply, out, size, 400, err);
        } else if (app_ui_replace_presets(&presets) != ESP_OK) {
            reply_error(reply, out, size, 500, "presets.json wasn't saved");
        } else {
            reply_text(reply, out, size, ui_presets_to_json(app_presets(), (char *)out, size));
        }
    } else if (strcmp(path, "/api/preview.bmp") == 0 && (get || strcmp(method, "POST") == 0)) {
        preview(method, query, body, out, size, reply);
    } else if (strcmp(path, "/api/screenshot.bmp") == 0 && get) {
        reply_bmp(display_fb(), out, size, reply);
    } else if (strcmp(path, "/api/time") == 0 && strcmp(method, "POST") == 0) {
        set_time(body, out, size, reply);
    } else if (strcmp(path, "/api/battery/learn") == 0 && strcmp(method, "POST") == 0) {
        learn(body, out, size, reply);
    } else if (strcmp(path, "/api/sync") == 0 && strcmp(method, "POST") == 0) { /* spec §10.3, M5 */
        esp_err_t e = app_sync_now();
        if (e == ESP_OK) {
            reply_text(reply, out, size, (size_t)snprintf((char *)out, size, "{\"started\":true}"));
            reply->status = 202; /* the page follows it in GET /api/status */
        } else {
            reply_error(reply, out, size, 409,
                        e == ESP_ERR_NOT_FOUND       ? "no Wi-Fi network is saved"
                        : e == ESP_ERR_INVALID_STATE ? "not now: a sync runs, the battery is critical, or the device "
                                                       "is on its own network only"
                                                     : "the sync didn't start");
        }
    } else if (strcmp(path, "/api/backup") == 0 && get) {
        backup(out, size, reply);
    } else if (strcmp(path, "/api/restore") == 0 && strcmp(method, "POST") == 0) {
        restore(body, out, size, reply);
    } else {
        reply_error(reply, out, size, 404, "no such API");
    }
}
