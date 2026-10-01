#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_internal.h"
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

static const char *TAG = "app_sync";

static bool s_active;           /* a sync runs, or its report waits for apply(): no other may start */
static bool s_manual;           /* the running sync was asked for: it ends with a toast */
static sync_due_t s_started_by; /* what started the running sync ({0, false} on demand) */

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
        sync_need_t need = !timekeeping_valid()                           ? SYNC_NEED_TIME
                           : always_wanted(now) && !s_active && wifi_off() ? SYNC_NEED_WIFI
                           : ds_weather(app_ds()) == NULL                  ? SYNC_NEED_FORECAST
                                                                           : SYNC_NEED_NOTHING;
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

bool app_sync_failed(void)
{
    const app_sync_state_t *s = st();
    if (s->last_at == 0) {
        return false;
    }
    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
        if (s->last_result[i] != SYNC_STEP_OK) {
            return true;
        }
    }
    return false;
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
    s->last_detail[0] = '\0';
    s->last_failed_step = SYNC_STEP_COUNT;
    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
        s->last_result[i] = r->result[i];
        if (r->result[i] != SYNC_STEP_OK && s->last_failed_step == SYNC_STEP_COUNT) {
            s->last_failed_step = (uint8_t)i;
            snprintf(s->last_detail, sizeof(s->last_detail), "%s", r->detail[i]);
        }
    }
}

static int64_t s_started_mono; /* esp_timer µs at the start, to date it once the clock is right */

/* The report, on the app task. The EV_CALL around it sees the clock move and calls app_clock_moved(). */
static void apply(void *arg)
{
    const sync_report_t *r = arg;
    if (r->result[SYNC_STEP_TIME] == SYNC_STEP_OK) {
        int64_t moved_ms = 0;
        esp_err_t err = timekeeping_apply_true_time(r->ntp_utc_us, r->ntp_mono_us, &moved_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "setting the time: %s", esp_err_to_name(err));
        } else if (moved_ms > 1000 || moved_ms < -1000) {
            ESP_LOGI(TAG, "the clock moved %lld ms", (long long)moved_ms);
        }
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
    time_t started = now - (time_t)((esp_timer_get_time() - s_started_mono) / 1000000);
    save_summary(r, started);
    bool ok = !app_sync_failed();
    sync_history_record(&st()->history, s_started_by, ok, now);
    ESP_LOGI(TAG, "sync %s%s%s", ok ? "done" : "failed at ", ok ? "" : sync_step_name(st()->last_failed_step),
             ok ? "" : st()->last_detail);
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

static void done(const sync_report_t *report) /* on the sync task */
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
    sync_request_t req = { .lat_e4 = set->lat_e4, .lon_e4 = set->lon_e4 };
    memcpy(req.ntp, set->ntp, sizeof(req.ntp));
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
    return start(true, (sync_due_t){ 0 });
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

void app_sync_tick(void)
{
    time_t now = time(NULL);
    bool critical = app_state()->critical;
    app_sync_wifi_check();
    if (critical || app_ui_night()) {
        return; /* nothing may drain the battery, and the night runs nothing (spec §9.1) */
    }
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
    if (s_active) {
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
    static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI, LS_SYNC_STEP_TIME, LS_SYNC_STEP_WEATHER,
                                                         LS_SYNC_STEP_AIR };
    if (s->last_failed_step >= SYNC_STEP_COUNT) {
        snprintf(out, size, "%s%s%s OK", when, suffix[0] ? " " : "", suffix);
    } else {
        snprintf(out, size, "%s%s%s %s: %s", when, suffix[0] ? " " : "", suffix,
                 lang_str(lang, k_steps[s->last_failed_step]), s->last_detail);
    }
}
