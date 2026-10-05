#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "storage.h"
#include "ui_solar.h"
#include "util_snapshot.h"
#include "util_time.h"

/* The PV forecast and the house's energy (spec §11.5, §11.6, M6d): what the Solar and Energy steps
 * bring, kept with the app's state through deep sleep and in /fs/state/solar.bin, and the view the
 * dashboard draws. It belongs to the app task. */

#define SOLAR_PATH "/fs/state/solar.bin"
#define SOLAR_MAGIC 0x7266736cu /* "rfsl" */
#define SOLAR_FILE_VERSION 1
#define READINGS_SAVE_S (30 * 60) /* spec §11.6: the readings go to the file at most every 30 min */

static const char *TAG = "app_solar";

typedef struct {
    util_snapshot_hdr_t hdr;
    solar_forecast_t forecast;
    uint32_t solcast_asked;
    uint8_t solcast_sites; /* its wait, so a two-site forecast stays fresh 6 h after a cold boot */
    energy_reading_t reading;
    energy_day_t day;
} solar_file_t;

static app_solar_state_t *st(void)
{
    return &app_state()->solar;
}

/* The real state, the demo's never being in it (it draws from its own sample day). */
static void save(void)
{
    EXT_RAM_BSS_ATTR static solar_file_t f;
    app_solar_state_t *s = st();
    if (storage_init() != ESP_OK) {
        return;
    }
    memset(&f, 0, sizeof(f));
    f.forecast = s->forecast;
    f.solcast_asked = s->solcast_asked;
    f.solcast_sites = s->solcast_sites;
    f.reading = s->reading;
    f.day = s->day;
    util_snapshot_seal(&f, sizeof(f), SOLAR_MAGIC, SOLAR_FILE_VERSION);
    esp_err_t err = storage_write_atomic(SOLAR_PATH, (const char *)&f, sizeof(f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not saved: %s", SOLAR_PATH, esp_err_to_name(err));
        return;
    }
    s->saved_at = (uint32_t)time(NULL);
}

void app_solar_restore(void)
{
    EXT_RAM_BSS_ATTR static solar_file_t f;
    if (!storage_ready()) {
        return;
    }
    FILE *file = fopen(SOLAR_PATH, "rb");
    if (file == NULL) {
        return;
    }
    size_t n = fread(&f, 1, sizeof(f), file);
    fclose(file);
    if (n != sizeof(f) || !util_snapshot_valid(&f, sizeof(f), SOLAR_MAGIC, SOLAR_FILE_VERSION)) {
        ESP_LOGW(TAG, "%s: not of this firmware; left out", SOLAR_PATH);
        return;
    }
    app_solar_state_t *s = st();
    s->forecast = f.forecast;
    s->solcast_asked = f.solcast_asked;
    s->solcast_sites = f.solcast_sites;
    s->reading = f.reading;
    s->day = f.day;
    s->saved_at = (uint32_t)time(NULL);
    ESP_LOGI(TAG, "forecast from %lu and reading from %lu restored", (unsigned long)f.forecast.fetched,
             (unsigned long)f.reading.at);
}

void app_solar_forecast_done(const solar_acc_t *acc, const char *error, const char *kept, uint32_t asked,
                             uint8_t sites)
{
    app_solar_state_t *s = st();
    uint32_t now = (uint32_t)time(NULL);
    s->forecast_tried = now;
    snprintf(s->forecast_kept, sizeof(s->forecast_kept), "%s", kept != NULL ? kept : "");
    if (kept != NULL && asked == 0) {
        return; /* nothing new to keep: the last call's error and the file stay as they are */
    }
    if (sites != 0) {
        s->solcast_sites = sites;
    }
    if (asked != 0) {
        s->solcast_asked = asked; /* failed calls count against the budget too (spec §11.5) */
    }
    if (acc != NULL) {
        solar_acc_finish(acc, &s->forecast, now, &s->forecast); /* gaps keep the last forecast's quarter hours */
    }
    if (kept == NULL) {
        snprintf(s->forecast_error, sizeof(s->forecast_error), "%s", error != NULL ? error : "");
    }
    save(); /* spec §11.5: after each Solar step */
}

void app_solar_budget_reset(void)
{
    st()->solcast_asked = 0;
    save();
}

void app_solar_reading_done(const energy_reading_t *r, const char *error)
{
    app_solar_state_t *s = st();
    uint32_t now = (uint32_t)time(NULL);
    s->energy_tried = now;
    snprintf(s->energy_error, sizeof(s->energy_error), "%s", error != NULL ? error : "");
    if (r == NULL) {
        return;
    }
    s->reading = *r;
    energy_day_add(&s->day, r);
    if (s->saved_at == 0 || now < s->saved_at || now - s->saved_at >= READINGS_SAVE_S) {
        save();
    }
}

void app_solar_demo(bool on)
{
    st()->demo = on;
    ESP_LOGI(TAG, "the sample day %s", on ? "shows" : "is gone");
}

/* The local midnight of `now` and its day. */
static int32_t today(time_t now, time_t *midnight)
{
    struct tm lt;
    localtime_r(&now, &lt);
    struct tm m = { .tm_year = lt.tm_year, .tm_mon = lt.tm_mon, .tm_mday = lt.tm_mday, .tm_isdst = -1 };
    *midnight = mktime(&m);
    return (int32_t)util_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

const ui_solar_t *app_solar_ui(void)
{
    static ui_solar_t view;
    EXT_RAM_BSS_ATTR static solar_forecast_t demo_forecast;
    EXT_RAM_BSS_ATTR static energy_reading_t demo_reading;
    EXT_RAM_BSS_ATTR static energy_day_t demo_day;
    const settings_t *set = app_settings();
    app_solar_state_t *s = st();
    if (s->demo) { /* spec §15: the goldens' sample day, today, its readings until now */
        time_t now = time(NULL), midnight;
        int32_t day = today(now, &midnight);
        bool battery = set->energy_battery == SETTINGS_BATTERY_ON;
        ui_solar_demo(day, midnight, now, true, battery, &demo_forecast, &demo_reading, &demo_day);
        view = (ui_solar_t){ .forecast = &demo_forecast, .reading = &demo_reading, .day = &demo_day,
                             .battery = battery };
        return &view;
    }
    view = (ui_solar_t){
        .forecast = &s->forecast,
        .forecast_ttl_s = solar_fresh_s((solar_source_t)set->solar_source, s->solcast_sites, app_sync_expected_s()),
        .reading = &s->reading,
        .day = &s->day,
        .battery = energy_battery_shown((energy_battery_t)set->energy_battery, &s->reading),
    };
    return &view;
}

void app_solar_request(sync_solar_req_t *solar, sync_energy_req_t *energy)
{
    const settings_t *set = app_settings();
    memset(solar, 0, sizeof(*solar));
    solar->source = set->solar_source;
    solar->plane_count = set->solar_plane_count;
    for (int i = 0; i < set->solar_plane_count && i < SOLAR_PLANES_MAX; i++) {
        const settings_plane_t *p = &set->solar_planes[i];
        solar->planes[i] = (solar_plane_t){ .kwp = p->kwp_e2 / 100.0f, .tilt = p->tilt, .azimuth = p->azimuth };
    }
    solar->losses_pct = set->solar_losses_pct;
    solar->inverter_w = set->solar_inverter_kw_e2 * 10.0f; /* hundredths of kW */
    if (set->solar_source == SETTINGS_SOLAR_FORECAST_SOLAR) {
        app_secret_get(SETTINGS_SECRET_FS_KEY, solar->key, sizeof(solar->key));
    } else if (set->solar_source == SETTINGS_SOLAR_SOLCAST) {
        app_secret_get(SETTINGS_SECRET_SOLCAST_KEY, solar->key, sizeof(solar->key));
        app_secret_get(SETTINGS_SECRET_SOLCAST_SITE1, solar->sites[0], sizeof(solar->sites[0]));
        app_secret_get(SETTINGS_SECRET_SOLCAST_SITE2, solar->sites[1], sizeof(solar->sites[1]));
    }
    solar->solcast_asked = st()->solcast_asked;
    memset(energy, 0, sizeof(*energy));
    energy->on = set->energy_source == SETTINGS_ENERGY_SOLAX;
    if (energy->on) {
        app_secret_get(SETTINGS_SECRET_SOLAX_TOKEN, energy->token, sizeof(energy->token));
        app_secret_get(SETTINGS_SECRET_SOLAX_SN, energy->sn, sizeof(energy->sn));
    }
}

const app_solar_state_t *app_solar_state(void)
{
    return st();
}
