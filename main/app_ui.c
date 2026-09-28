#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "ui_dashboard.h"
#include "util_time.h"

static const char *TAG = "app_ui";

static app_ui_state_t s;
static char s_file[6144]; /* presets.json with 16 presets is about 5 KB */
static char s_err[96];

static void default_settings(settings_t *out)
{
    *out = (settings_t){
        .language = "en",
        .clock_24h = true,
        .sensors_every_min = CONFIG_REFLBO_SENSOR_INTERVAL_MIN,
        .temp_offset_c100 = CONFIG_REFLBO_TEMP_OFFSET_C10 * 10,
        .hum_offset_pct100 = CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10,
        .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
        .lpm_quarter_hz = 4, /* 1 Hz (D12) */
    };
    snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
    snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
}

void app_ui_defaults(void)
{
    memset(&s, 0, sizeof(s));
    ds_init(&s.ds);
    default_settings(&s.settings);
    ui_presets_defaults(&s.presets);
}

static bool parse_settings(const char *text, void *ctx)
{
    settings_t defaults;
    default_settings(&defaults);
    bool ok = settings_from_json(text, &defaults, ctx, s_err, sizeof(s_err));
    if (!ok) {
        ESP_LOGW(TAG, "settings.json: %s", s_err);
    }
    return ok;
}

static bool parse_presets(const char *text, void *ctx)
{
    bool ok = ui_presets_from_json(text, ctx, s_err, sizeof(s_err));
    if (!ok) {
        ESP_LOGW(TAG, "presets.json: %s", s_err);
    }
    return ok;
}

static void load_one(const char *path, storage_parse_t parse, void *target, size_t size, const void *defaults)
{
    bool from_backup = false;
    esp_err_t err = storage_load(path, s_file, sizeof(s_file), parse, target, &from_backup);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s loaded%s", path, from_backup ? " from the backup" : "");
        return;
    }
    memcpy(target, defaults, size); /* a failed parse may have left it half-written */
    ESP_LOGI(TAG, "%s: %s; using defaults", path, err == ESP_ERR_NOT_FOUND ? "not there yet" : "invalid");
}

void app_ui_load(void)
{
    settings_t settings_defaults;
    default_settings(&settings_defaults);
    ui_presets_t presets_defaults;
    ui_presets_defaults(&presets_defaults);
    s.settings = settings_defaults;
    s.presets = presets_defaults;
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage: %s; settings and presets use their defaults", esp_err_to_name(err));
        return;
    }
    load_one(STORAGE_SETTINGS_PATH, parse_settings, &s.settings, sizeof(s.settings), &settings_defaults);
    load_one(STORAGE_PRESETS_PATH, parse_presets, &s.presets, sizeof(s.presets), &presets_defaults);
    s.cycle_at = 0; /* armed by the first tick, once the RTC has set the clock */
}

void app_ui_export(app_ui_state_t *out)
{
    *out = s;
}

void app_ui_import(const app_ui_state_t *in)
{
    s = *in;
}

const settings_t *app_settings(void)
{
    return &s.settings;
}

ds_t *app_ds(void)
{
    return &s.ds;
}

ui_presets_t *app_presets(void)
{
    return &s.presets;
}

void app_ui_apply_settings(void)
{
    timekeeping_init(s.settings.tz_posix);
    sensors_set_offsets(s.settings.temp_offset_c100, s.settings.hum_offset_pct100);
    /* Spec §5.1: stale after 15 min, but never before the next reading is due. */
    uint32_t ttl = (uint32_t)s.settings.sensors_every_min * 120u;
    ttl = ttl < 900 ? 900 : ttl;
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        ds_set_ttl(&s.ds, (ds_field_t)f, ttl);
    }
    int quarter_hz = s.settings.lpm_quarter_hz, rate = 0;
    while (quarter_hz > 1 && rate < ST7305_LPM_8HZ) {
        quarter_hz /= 2;
        rate++;
    }
    if (display_fb() != NULL && st7305_lpm_rate() != (st7305_lpm_rate_t)rate) {
        esp_err_t err = st7305_set_lpm_rate((st7305_lpm_rate_t)rate);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "panel rate: %s", esp_err_to_name(err));
        }
    }
}

static int32_t local_day(const struct tm *local)
{
    return (int32_t)util_days_from_civil(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
}

void app_ui_context(ui_context_t *ctx)
{
    time_t now = time(NULL);
    *ctx = (ui_context_t){ .now = now, .time_valid = timekeeping_valid(), .ds = &s.ds,
                           .lang = lang_get(s.settings.language), .clock_24h = s.settings.clock_24h,
                           .fahrenheit = s.settings.fahrenheit };
    localtime_r(&now, &ctx->local);
    ctx->local_day = local_day(&ctx->local);
}

void app_ui_render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    ui_context_t ctx;
    app_ui_context(&ctx);
    ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

static ds_bat_state_t ds_battery_state(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return DS_BAT_DISCHARGING;
    case BATTERY_CHARGING:
        return DS_BAT_CHARGING;
    case BATTERY_FULL:
        return DS_BAT_FULL;
    default:
        return DS_BAT_UNKNOWN;
    }
}

void app_ui_sample(time_t now)
{
    struct tm local;
    localtime_r(&now, &local);
    esp_err_t err = sensors_sample_env(now);
    if (err == ESP_OK) {
        sensors_env_t env = sensors_env();
        ds_set_env(&s.ds, env.temp_c100, env.hum_pct100, env.time, local_day(&local));
    } else {
        ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
    }
    err = sensors_sample_battery(now);
    if (err == ESP_OK) {
        sensors_battery_t bat = sensors_battery(now);
        ds_set_battery(&s.ds, bat.level, bat.smoothed_mv, ds_battery_state(bat.state), now);
        int days10 = sensors_battery_days_left10(now);
        if (days10 >= 0) {
            ds_set(&s.ds, DS_BAT_DAYS, days10, now);
        } else {
            ds_clear(&s.ds, DS_BAT_DAYS);
        }
    } else {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
    ds_take_changes(&s.ds); /* every sample is followed by a render anyway */
}

static void save_presets(void)
{
    size_t n = ui_presets_to_json(&s.presets, s_file, sizeof(s_file));
    if (n == 0) {
        ESP_LOGE(TAG, "presets.json does not fit %u bytes", (unsigned)sizeof(s_file));
        return;
    }
    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
    if (err == ESP_OK) {
        err = storage_write_atomic(STORAGE_PRESETS_PATH, s_file, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving presets: %s", esp_err_to_name(err));
    }
}

void app_ui_select(int index, bool persist)
{
    if (index < 0 || index >= s.presets.count) {
        return;
    }
    s.presets.active = (uint8_t)index;
    if (s.presets.cycle_enabled) {
        s.cycle_at = time(NULL) + s.presets.cycle_interval_s; /* a switch restarts the interval */
    }
    ESP_LOGI(TAG, "preset %s", s.presets.presets[index].id);
    app_ui_render();
    if (persist) {
        save_presets();
    }
}

void app_ui_toggle_cycle(void)
{
    s.presets.cycle_enabled = !s.presets.cycle_enabled;
    s.cycle_at = s.presets.cycle_enabled ? time(NULL) + s.presets.cycle_interval_s : 0;
    ESP_LOGI(TAG, "auto-cycle %s", s.presets.cycle_enabled ? "on" : "off");
    save_presets();
}

void app_ui_tick(bool force)
{
    time_t now = time(NULL);
    time_t slot = now - now % 60;
    bool new_slot = slot != s.done_slot;
    bool render = force || s.presets.presets[s.presets.active].seconds;
    if (force || (new_slot && scheduler_is_slot(slot, s.settings.sensors_every_min))) {
        app_ui_sample(now);
        render = true;
    }
    if (new_slot && scheduler_is_slot(slot, s.settings.display_every_min)) {
        render = true;
    }
    s.done_slot = slot;
    if (s.presets.cycle_enabled && (s.cycle_at == 0 || s.cycle_at - now > s.presets.cycle_interval_s)) {
        s.cycle_at = now + s.presets.cycle_interval_s; /* the first tick, or the clock moved back */
    }
    if (s.presets.cycle_enabled && now >= s.cycle_at) {
        app_ui_select(ui_presets_next(&s.presets), false); /* renders; not saved, the cycle will move on */
        return;
    }
    if (render) {
        app_ui_render();
    }
}

sched_wake_t app_ui_next_wake(time_t now)
{
    sched_input_t in = {
        .now = now,
        .display_every_min = s.settings.display_every_min,
        .sensors_every_min = s.settings.sensors_every_min,
        .cycle_at = s.presets.cycle_enabled ? s.cycle_at : 0,
        .every_second = s.presets.presets[s.presets.active].seconds,
    };
    return scheduler_next_wake(&in);
}
