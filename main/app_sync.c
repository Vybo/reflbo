#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_internal.h"
#include "energy_mqtt.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lang.h"
#include "netmgr.h"
#include "sync.h"
#include "sync_plan.h"
#include "timekeeping.h"
#include "webui.h"

/* When syncs run and what their reports change (spec §9.3, D25). It belongs to the app task: the
 * sync task fetches, this file applies, and sync mode `always` keeps Wi-Fi up between refreshes. */

#define LOW_BATTERY_PCT 15 /* spec §8: no retries */
#define WEB_REPLY_MS 3000  /* a web request just served gets its reply out before Wi-Fi goes */
#define ENERGY_REFRESH_S 300 /* spec §9.3: in sync mode `always`, a reading every 5 min (D36) */

static const char *TAG = "app_sync";

static bool s_active;           /* a sync runs, or its report waits for apply(): no other may start */
static bool s_manual;           /* the running sync was asked for: it ends with a toast */
static sync_due_t s_started_by; /* what started the running sync ({0, false} on demand) */
static time_t s_radar_next;     /* sync mode `always`: the next radar refresh; 0 = at once */
static time_t s_energy_next;    /* and the next reading of the house's energy (M6d) */
static bool s_refresh;          /* what runs is such a refresh, or a check (M6d), not a sync */
static bool s_manual_waiting;   /* a sync asked for during such a refresh: it starts when that ends */

static bool always_wanted(time_t now);
static bool wifi_off(void);

static app_sync_state_t *st(void)
{
    return &app_state()->sync;
}

static void schedule_from_settings(sync_schedule_t *out)
{
    const settings_t *s = app_settings();
    *out = (sync_schedule_t){ .mode = s->sync_mode, .time_count = s->sync_time_count,
                              .interval_min = s->sync_interval_min, .quiet = s->quiet,
                              .quiet_from = s->quiet_from, .quiet_to = s->quiet_to };
    memcpy(out->times, s->sync_times, sizeof(out->times));
}

static bool low_battery(void)
{
    ds_entry_t bat;
    return ds_get(app_ds(), DS_BAT_LEVEL, &bat) && bat.value <= LOW_BATTERY_PCT;
}

static bool networks_saved(void)
{
    if (app_net_init() != ESP_OK) {
        return false;
    }
    static netmgr_list_t list;
    netmgr_networks(&list);
    return list.count > 0;
}

uint32_t app_sync_expected_s(void)
{
    sync_schedule_t s;
    schedule_from_settings(&s);
    return sync_expected_interval_s(&s, time(NULL));
}

void app_sync_schedule(void)
{
    sync_schedule_t s;
    schedule_from_settings(&s);
    time_t now = time(NULL);
    uint32_t expected = sync_expected_interval_s(&s, now);
    ds_set_forecast_ttl(app_ds(), expected == 0 ? 0 : expected + 2 * 3600); /* spec §5.1 */
    if (!networks_saved()) {
        st()->due = (sync_due_t){ 0 }; /* nothing to join: no sync wakes the board */
    } else {
        /* spec §3.3, §7: a lost time at once, then with the retries' pauses; `always` mode's Wi-Fi and
         * the first forecast at once, unless a sync failed since */
        sync_need_t need = sync_need(timekeeping_valid(), always_wanted(now) && !s_active && wifi_off(),
                                     ds_weather(app_ds()) != NULL, app_settings()->sync_steps & SETTINGS_STEP_WEATHER);
        st()->due = sync_next_due_needing(&s, &st()->history, now, low_battery(), need);
        if (st()->due.at > now && st()->due.at % 60 != 0) {
            st()->due.at += 60 - st()->due.at % 60; /* minute wakes use the RTC alarm (spec §9.2) */
        }
    }
    if (st()->due.at != 0) {
        struct tm local;
        localtime_r(&st()->due.at, &local);
        ESP_LOGI(TAG, "next sync %02d:%02d%s", local.tm_hour, local.tm_min, st()->due.retry ? " (a retry)" : "");
    }
}

time_t app_sync_due(void)
{
    return st()->due.at;
}

bool app_sync_active(void)
{
    return s_active;
}

bool app_sync_refreshing(void)
{
    return s_active && s_refresh;
}

bool app_sync_running(void)
{
    return (s_active && !s_refresh) || s_manual_waiting;
}

bool app_sync_failed(void)
{
    const app_sync_state_t *s = st();
    return s->last_at != 0 && sync_report_failed(s->last_result); /* not for the house's energy or MQTT (D36, D32) */
}

bool app_sync_mark_failed(void)
{
    return st()->sched_failed;
}

/* Wi-Fi is off: netmgr isn't up yet (a routine wake), or it says so. */
static bool wifi_off(void)
{
    if (!app_net_ready()) {
        return true;
    }
    netmgr_status_t ns;
    netmgr_status(&ns);
    return ns.state == NETMGR_OFF;
}

/* Sync mode `always` wants Wi-Fi now: on, unless quiet hours (D25) or a night (spec §9.1), which
 * sleeps whatever else runs. */
static bool always_wanted(time_t now)
{
    if (app_ui_night()) {
        return false;
    }
    sync_schedule_t s;
    schedule_from_settings(&s);
    return sync_wifi_wanted(&s, now);
}

bool app_sync_holds_wifi(void)
{
    if (!app_net_ready() || !always_wanted(time(NULL)) || app_state()->critical) {
        return false;
    }
    netmgr_status_t ns;
    netmgr_status(&ns);
    return ns.state == NETMGR_STATION || ns.state == NETMGR_JOINING;
}

bool app_sync_lan_ui(void)
{
    if (!app_net_ready() || !always_wanted(time(NULL)) || app_state()->critical) {
        return false;
    }
    netmgr_status_t ns;
    netmgr_status(&ns);
    return ns.state == NETMGR_STATION;
}

/* Wi-Fi off after a sync, unless config mode or sync mode `always` keeps it. */
static void release_wifi(void)
{
    if (!app_config_active() && !app_sync_holds_wifi()) {
        netmgr_stop();
    }
    app_net_refresh();
}

static void save_summary(const sync_report_t *r, time_t started)
{
    app_sync_state_t *s = st();
    s->last_at = (uint32_t)started;
    memcpy(s->last_result, r->result, sizeof(s->last_result));
    s->last_failed_step = (uint8_t)sync_first_failed(r->result); /* the house's energy too: Info shows it */
    memcpy(s->last_detail, r->detail, sizeof(s->last_detail));
}

static int64_t s_started_mono; /* esp_timer µs at the start, to date it once the clock is right */

static esp_err_t start(bool manual, sync_due_t due);

/* The Solar and Energy steps that ran, into the solar state (spec §11.5, §11.6). A step that kept the forecast
 * (Solcast's budget, a 429) shows its detail on the Sync page, not as an error. */
static void apply_solar(const sync_report_t *r)
{
    uint8_t solar = r->result[SYNC_STEP_SOLAR], energy = r->result[SYNC_STEP_ENERGY];
    if (solar != SYNC_STEP_NOT_RUN) {
        app_solar_forecast_done(solar == SYNC_STEP_OK ? r->solar : NULL,
                                solar == SYNC_STEP_FAILED ? r->detail[SYNC_STEP_SOLAR] : NULL,
                                solar == SYNC_STEP_KEPT ? r->detail[SYNC_STEP_SOLAR] : NULL, r->solcast_asked,
                                r->solcast_sites);
    }
    if (energy != SYNC_STEP_NOT_RUN) {
        app_solar_dev_keep(r);
        app_solar_reading_done(energy == SYNC_STEP_OK ? &r->energy : NULL,
                               energy == SYNC_STEP_FAILED ? r->detail[SYNC_STEP_ENERGY] : NULL);
    }
}

/* The radar's frames and status, when its step ran or brought frames: a refresh for the house's reading alone, a
 * check, or a sync with the radar switched off leaves a playing loop be (spec §11.2). */
static void apply_radar(sync_report_t *r)
{
    if (r->result[SYNC_STEP_RADAR] != SYNC_STEP_NOT_RUN || r->radar.count > 0) {
        app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
    }
}

/* A refresh's or a check's report (spec §9.3): the frames, the reading, the forecast, outside the syncs' history
 * and retries. */
static void apply_refresh(sync_report_t *r)
{
    apply_radar(r);
    apply_solar(r);
    s_active = false;
    s_refresh = false;
    if (s_manual_waiting) {
        s_manual_waiting = false;
        if (start(true, (sync_due_t){ 0 }) != ESP_OK) {
            app_ui_toast(lang_str(lang_get(app_settings()->language), LS_T_SYNC_FAILED));
        }
        return;
    }
    app_ui_render();
}

/* The report, on the app task. The EV_CALL around it sees the clock move and calls app_clock_moved(). */
static void apply(void *arg)
{
    sync_report_t *r = arg;
    if (r->kind != SYNC_KIND_SYNC) {
        if (r->kind == SYNC_KIND_CHECK) {
            release_wifi(); /* as a sync does: config mode or `always` keep it */
        }
        apply_refresh(r);
        return;
    }
    int64_t moved_ms = 0;
    if (r->result[SYNC_STEP_TIME] == SYNC_STEP_OK) {
        esp_err_t err = timekeeping_apply_true_time(r->ntp_utc_us, r->ntp_mono_us, &moved_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "setting the time: %s", esp_err_to_name(err));
        } else if (moved_ms > 1000 || moved_ms < -1000) {
            ESP_LOGI(TAG, "the clock moved %lld ms", (long long)moved_ms);
        }
    }
    if (r->energy_local && moved_ms != 0) { /* D40: after a lost clock (D9) its values came dated in 2000 */
        energy_mqtt_shift(&r->energy, (moved_ms + (moved_ms >= 0 ? 500 : -500)) / 1000);
    }
    time_t now = time(NULL);
    if (r->result[SYNC_STEP_WEATHER] == SYNC_STEP_OK) {
        ds_weather_t w = r->weather;
        w.fetched = (uint32_t)now;
        ds_set_weather(app_ds(), &w);
    }
    if (r->result[SYNC_STEP_AIR] == SYNC_STEP_OK) {
        ds_air_t a = r->air;
        a.fetched = (uint32_t)now;
        ds_set_air(app_ds(), &a);
    }
    if (r->result[SYNC_STEP_WEATHER] == SYNC_STEP_OK || r->result[SYNC_STEP_AIR] == SYNC_STEP_OK) {
        app_ui_save_forecast(); /* spec §6: it survives a power-off, shown as stale */
    }
    apply_radar(r);
    apply_solar(r);
    time_t started = now - (time_t)((esp_timer_get_time() - s_started_mono) / 1000000);
    save_summary(r, started);
    bool ok = !app_sync_failed();
    sync_history_record(&st()->history, s_started_by, ok, now);
    if (ok) {
        st()->last_ok_at = (uint32_t)started;
        st()->sched_failed = false;
    } else if (s_started_by.at != 0) {
        st()->sched_failed = true; /* spec §5.2: a scheduled sync's, not one on demand (M5 review) */
    }
    ESP_LOGI(TAG, "sync %s%s%s%s", ok ? "done" : "failed at ", ok ? "" : sync_step_name(st()->last_failed_step),
             ok ? "" : ": ", ok ? "" : st()->last_detail[st()->last_failed_step]); /* "failed at weather: HTTP 503" */
    if (r->result[SYNC_STEP_MQTT] == SYNC_STEP_FAILED) {
        ESP_LOGW(TAG, "MQTT: %s", r->detail[SYNC_STEP_MQTT]);
    }
    s_active = false; /* the report is applied: the next sync may overwrite it */
    release_wifi();
    app_sync_schedule();
    if (s_manual) {
        const lang_t *lang = lang_get(app_settings()->language);
        app_ui_toast(lang_str(lang, ok ? LS_T_SYNC_DONE : LS_T_SYNC_FAILED));
    } else {
        app_ui_render();
    }
}

static void done(sync_report_t *report) /* on the sync task */
{
    /* The report must arrive: until apply() runs, s_active holds every other sync and sleep off. */
    while (app_post(apply, (void *)report) != ESP_OK) {
        ESP_LOGW(TAG, "the app queue is full; the sync's report waits");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static esp_err_t start(bool manual, sync_due_t due)
{
    if (s_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (app_net_init() != ESP_OK) { /* it brings NVS up first: a routine wake has none (gotcha 11) */
        return ESP_FAIL;
    }
    const settings_t *set = app_settings();
    EXT_RAM_BSS_ATTR static sync_request_t req; /* the radar's and the solar parts are large for the stack */
    req = (sync_request_t){ .kind = SYNC_KIND_SYNC, .steps = set->sync_steps, .lat_e4 = set->lat_e4,
                            .lon_e4 = set->lon_e4, .now = timekeeping_valid() ? (uint32_t)time(NULL) : 0 };
    memcpy(req.ntp, set->ntp, sizeof(req.ntp));
    app_radar_request(&req.radar);
    app_solar_request(&req.solar, &req.energy);
    if (app_mqtt_on()) { /* M7 (spec §9.3 step 8) */
        app_mqtt_prepare();
        req.mqtt = app_mqtt_sync_step;
        req.energy_mqtt = set->energy_source == SETTINGS_ENERGY_MQTT ? app_mqtt_energy : NULL; /* D40 */
    }
    esp_err_t err = sync_start(&req, done);
    if (err == ESP_OK) {
        s_active = true;
        s_manual = manual;
        s_started_by = due;
        s_started_mono = esp_timer_get_time();
        ESP_LOGI(TAG, "sync starts%s", manual ? " (asked for)" : due.retry ? " (a retry)" : "");
        app_ui_render(); /* the status bar shows it running */
    }
    return err;
}

void app_sync_now_toast(void)
{
    esp_err_t err = app_sync_now();
    const lang_t *lang = lang_get(app_settings()->language);
    app_ui_toast(lang_str(lang, err == ESP_ERR_NOT_FOUND ? LS_T_NO_NETWORK : LS_T_SYNC_STARTED));
}

void app_sync_toggle_always(void)
{
    const lang_t *lang = lang_get(app_settings()->language);
    settings_t *set = &app_state()->settings;
    bool on = set->sync_mode != SETTINGS_SYNC_ALWAYS;
    if (app_state()->critical) {
        return; /* spec §8: nothing may drain the battery */
    }
    if (on && !networks_saved()) {
        app_ui_toast(lang_str(lang, LS_T_NO_NETWORK));
        return;
    }
    settings_toggle_always(set);
    app_ui_save_settings();
    app_clock_moved(0); /* as the menu's mode does: `always` takes Wi-Fi at once; leaving it, it goes */
    ESP_LOGI(TAG, "BOOT double: sync mode always %s", on ? "on" : "off");
    char text[64];
    if (!on) { /* "Sync: At set times": the packs keep the modes in settings_sync_mode_t order */
        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_SYNC_MODE),
                 lang_str(lang, (lang_str_t)(LS_SYNC_TIMES + set->sync_mode)));
    } else if (always_wanted(time(NULL))) {
        snprintf(text, sizeof(text), "%s", lang_str(lang, LS_T_ALWAYS_ON));
    } else { /* quiet hours (D25) or a night (spec §9.1) keep Wi-Fi off until they end */
        int from = set->quiet_to;
        if (app_ui_night()) {
            struct tm local;
            localtime_r(&app_state()->night_until, &local);
            from = local.tm_hour * 60 + local.tm_min;
        }
        snprintf(text, sizeof(text), "%s %02d:%02d", lang_str(lang, LS_T_ALWAYS_FROM), from / 60 % 24, from % 60);
    }
    app_ui_toast(text);
}

esp_err_t app_sync_now(void)
{
    if (app_state()->critical) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!networks_saved()) {
        return ESP_ERR_NOT_FOUND;
    }
    if (app_config_active()) {
        netmgr_status_t ns;
        netmgr_status(&ns);
        if (ns.state != NETMGR_STATION) {
            return ESP_ERR_INVALID_STATE; /* only the device's own network: nothing to reach */
        }
    }
    if (s_active && s_refresh) { /* the radar's refresh ends within 30 s: the sync follows it */
        s_manual_waiting = true;
        app_ui_render(); /* the status bar shows it running */
        return ESP_OK;
    }
    return start(true, (sync_due_t){ 0 });
}

esp_err_t app_sync_check(void)
{
    if (app_state()->critical) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!networks_saved()) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_active) {
        return ESP_ERR_INVALID_STATE; /* a sync or a refresh runs: they bring the same */
    }
    if (app_config_active()) {
        netmgr_status_t ns;
        netmgr_status(&ns);
        if (ns.state != NETMGR_STATION) {
            return ESP_ERR_INVALID_STATE; /* only the device's own network: nothing to reach */
        }
    }
    const settings_t *set = app_settings();
    if (set->solar_source == SETTINGS_SOLAR_OFF && set->energy_source == SETTINGS_ENERGY_OFF) {
        return ESP_ERR_INVALID_ARG; /* nothing to check */
    }
    EXT_RAM_BSS_ATTR static sync_request_t req;
    req = (sync_request_t){ .kind = SYNC_KIND_CHECK, .steps = SETTINGS_STEPS_ALL, .lat_e4 = set->lat_e4,
                            .lon_e4 = set->lon_e4, .now = timekeeping_valid() ? (uint32_t)time(NULL) : 0 };
    app_solar_request(&req.solar, &req.energy); /* asked for: the steps' switches don't hold it back */
    req.energy_mqtt = set->energy_source == SETTINGS_ENERGY_MQTT && app_mqtt_on() ? app_mqtt_energy : NULL; /* D40 */
    esp_err_t err = sync_start(&req, done);
    if (err == ESP_OK) {
        s_active = true;
        s_refresh = true; /* not a sync: no mark in the status bar, no history */
        s_manual = false;
        ESP_LOGI(TAG, "solar check starts");
    }
    return err;
}

bool app_sync_wifi_pending(void)
{
    if (!app_net_ready() || app_config_active() || s_active) {
        return false;
    }
    if (!app_state()->critical && always_wanted(time(NULL))) {
        return false;
    }
    return !wifi_off(); /* quiet hours or a night began in sync mode `always` (D25), or it ended */
}

void app_sync_wifi_check(void)
{
    static int64_t s_stopped_ms; /* netmgr_stop() was posted then: it takes a moment */
    int64_t now_ms = app_uptime_ms();
    if (!app_sync_wifi_pending() || now_ms - s_stopped_ms < WEB_REPLY_MS) {
        return;
    }
    if (!app_state()->critical && now_ms - webui_last_request_ms() < WEB_REPLY_MS) {
        return; /* a page changed the mode: its reply goes out first */
    }
    ESP_LOGI(TAG, "Wi-Fi off: nothing needs it");
    s_stopped_ms = now_ms;
    netmgr_stop();
    app_net_refresh();
}

/* Sync mode `always` on the network: the radar every 5 min (RainViewer: 10), at once when the mode begins, so
 * the loop's hour comes in one go (spec §9.3, D28); the house's reading every 5 min, in the radar's refresh when
 * one runs then (D36). A step that is off makes no requests (D35). */
static void refresh_tick(time_t now)
{
    if (!app_sync_lan_ui()) {
        s_radar_next = 0; /* the next time `always` holds Wi-Fi, the hour comes at once */
        s_energy_next = 0;
        return;
    }
    const settings_t *set = app_settings();
    bool radar = (set->sync_steps & SETTINGS_STEP_RADAR) && now >= s_radar_next;
    bool energy = (set->sync_steps & SETTINGS_STEP_ENERGY) && set->energy_source != SETTINGS_ENERGY_OFF &&
                  set->energy_source != SETTINGS_ENERGY_MQTT && now >= s_energy_next; /* MQTT's come by themselves */
    if (s_active || (!radar && !energy)) {
        return;
    }
    EXT_RAM_BSS_ATTR static sync_request_t req;
    req = (sync_request_t){ .kind = SYNC_KIND_REFRESH, .steps = set->sync_steps, .refresh_radar = radar,
                            .refresh_energy = energy, .now = timekeeping_valid() ? (uint32_t)now : 0 };
    if (radar) {
        app_radar_request(&req.radar);
    }
    if (energy) {
        app_solar_request(&req.solar, &req.energy);
    }
    if (sync_start(&req, done) == ESP_OK) {
        s_active = true;
        s_refresh = true;
        s_manual = false;
        if (radar) {
            s_radar_next = sync_radar_next(now, app_radar_step_s());
        }
        if (energy) {
            s_energy_next = sync_radar_next(now, ENERGY_REFRESH_S);
        }
        ESP_LOGI(TAG, "refresh:%s%s", radar ? " radar" : "", energy ? " energy" : "");
    }
}

void app_sync_tick(void)
{
    time_t now = time(NULL);
    bool critical = app_state()->critical;
    app_sync_wifi_check();
    if (critical || app_ui_night()) {
        return; /* nothing may drain the battery, and the night runs nothing (spec §9.1) */
    }
    refresh_tick(now);
    sync_due_t due = st()->due;
    if (!s_active && due.at > now && st()->history.failed_at == 0 && always_wanted(now) && wifi_off()) {
        app_sync_schedule(); /* a night ended in sync mode `always`: Wi-Fi back at once, not at the hour */
        due = st()->due;
    }
    if (due.at == 0 || now < due.at || s_active) {
        return;
    }
    if (app_config_active()) {
        netmgr_status_t ns;
        netmgr_status(&ns);
        if (ns.state != NETMGR_STATION) {
            return; /* spec §9.3: on the AP alone it waits until config mode ends */
        }
    }
    if (start(false, due) != ESP_OK) { /* no task for it: counts as failed, so the retries follow */
        ESP_LOGE(TAG, "the sync didn't start");
        sync_history_record(&st()->history, due, false, now);
        app_sync_schedule();
    }
}

void app_sync_summary(char *out, size_t size)
{
    const lang_t *lang = lang_get(app_settings()->language);
    const app_sync_state_t *s = st();
    if (app_sync_running()) {
        snprintf(out, size, "%s", lang_str(lang, LS_SYNC_RUNNING));
        return;
    }
    if (s->last_at == 0) {
        snprintf(out, size, "%s", lang_str(lang, LS_SYNC_NEVER));
        return;
    }
    time_t at = s->last_at;
    struct tm local;
    localtime_r(&at, &local);
    char when[12];
    const char *suffix;
    lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
    static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI,  LS_SYNC_STEP_TIME,  LS_SYNC_STEP_WEATHER,
                                                         LS_SYNC_STEP_AIR,   LS_SYNC_STEP_RADAR, LS_SYNC_STEP_SOLAR,
                                                         LS_SYNC_STEP_ENERGY, LS_SYNC_STEP_MQTT };
    if (s->last_failed_step >= SYNC_STEP_COUNT) {
        snprintf(out, size, "%s%s%s OK", when, suffix[0] ? " " : "", suffix);
    } else {
        snprintf(out, size, "%s%s%s %s: %s", when, suffix[0] ? " " : "", suffix,
                 lang_str(lang, k_steps[s->last_failed_step]), s->last_detail[s->last_failed_step]);
    }
}
