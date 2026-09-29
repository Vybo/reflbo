#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "power.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "ui_dashboard.h"
#include "ui_screens.h"
#include "util_time.h"

#define TOAST_MS 3000

static const char *TAG = "app_ui";

static app_ui_state_t s;
static char s_file[UI_PRESETS_JSON_MAX];
static char s_settings_base[2048]; /* settings.json as read: keys this firmware doesn't know stay */
static char s_err[96];
static char s_toast[64];
static int64_t s_toast_until_ms;

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

/* True if the file had to fall back to the defaults (it existed but nothing in it parsed). */
static bool load_one(const char *path, char *buf, size_t buf_size, storage_parse_t parse, void *target, size_t size,
                     const void *defaults)
{
    bool from_backup = false;
    esp_err_t err = storage_load(path, buf, buf_size, parse, target, &from_backup);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s loaded%s", path, from_backup ? " from the backup" : "");
        return false;
    }
    memcpy(target, defaults, size); /* a failed parse may have left it half-written */
    ESP_LOGI(TAG, "%s: %s; using defaults", path, err == ESP_ERR_NOT_FOUND ? "not there yet" : "invalid");
    return err != ESP_ERR_NOT_FOUND;
}

void app_ui_load(void)
{
    settings_t settings_defaults;
    default_settings(&settings_defaults);
    ui_presets_t presets_defaults;
    ui_presets_defaults(&presets_defaults);
    s.settings = settings_defaults;
    s.presets = presets_defaults;
    s.cycle_at = 0; /* armed by the first tick, once the RTC has set the clock */
    s_settings_base[0] = '\0';
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage: %s; settings and presets use their defaults", esp_err_to_name(err));
        return;
    }
    bool fell_back = load_one(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), parse_settings,
                              &s.settings, sizeof(s.settings), &settings_defaults);
    if (fell_back) {
        s_settings_base[0] = '\0'; /* nothing worth keeping in it */
    }
    fell_back |= load_one(STORAGE_PRESETS_PATH, s_file, sizeof(s_file), parse_presets, &s.presets, sizeof(s.presets),
                          &presets_defaults);
    if (fell_back) { /* spec §14.3: say so */
        app_ui_toast(lang_str(lang_get(s.settings.language), LS_T_DEFAULTS));
    }
}

void app_ui_export(app_ui_state_t *out)
{
    *out = s;
}

void app_ui_import(const app_ui_state_t *in)
{
    s = *in;
}

app_ui_state_t *app_state(void)
{
    return &s;
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
    if (s.cold_boot_at == 0 && timekeeping_valid()) {
        s.cold_boot_at = time(NULL); /* Info > Uptime counts from the first valid clock after a cold boot */
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
    if (fb == NULL || display_asleep()) {
        return; /* night sleep: nothing is drawn (spec §9.1) */
    }
    if (app_menu_is_open()) {
        app_menu_render();
        return;
    }
    ui_context_t ctx;
    app_ui_context(&ctx);
    if (s.critical) {
        ui_draw_critical(fb, &ctx);
    } else {
        ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
    }
    if (app_ui_toast_active()) {
        ui_draw_toast(fb, s_toast);
    }
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

void app_ui_toast(const char *text)
{
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toast_until_ms = app_uptime_ms() + TOAST_MS;
    power_hold_awake_ms(TOAST_MS); /* a sleep would leave it on screen until the next wake */
    ESP_LOGI(TAG, "toast: %s", text);
    app_ui_render();
}

bool app_ui_toast_active(void)
{
    return s_toast[0] != '\0' && app_uptime_ms() < s_toast_until_ms;
}

int64_t app_ui_toast_until_ms(void)
{
    return s_toast[0] != '\0' ? s_toast_until_ms : 0;
}

void app_ui_toast_expire(void)
{
    if (s_toast[0] != '\0' && !app_ui_toast_active()) {
        s_toast[0] = '\0';
        app_ui_render();
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
        bool critical = battery_critical(s.critical, bat.smoothed_mv, bat.state); /* spec §8 */
        if (critical && !s.critical) {
            ESP_LOGW(TAG, "battery critical: %d mV", bat.smoothed_mv);
        } else if (!critical && s.critical) {
            ESP_LOGI(TAG, "battery recovered: %d mV", bat.smoothed_mv);
        }
        s.critical = critical;
    } else {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
    ds_take_changes(&s.ds); /* every sample is followed by a render anyway */
}

esp_err_t app_ui_save_presets(void)
{
    size_t n = ui_presets_to_json(&s.presets, s_file, sizeof(s_file));
    if (n == 0) {
        ESP_LOGE(TAG, "presets.json does not fit %u bytes", (unsigned)sizeof(s_file));
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
    if (err == ESP_OK) {
        err = storage_write_atomic(STORAGE_PRESETS_PATH, s_file, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving presets: %s", esp_err_to_name(err));
    }
    return err;
}

static bool keep_text(const char *text, void *ctx)
{
    (void)text;
    (void)ctx;
    return true; /* storage_load left it in s_settings_base */
}

esp_err_t app_ui_save_settings(void)
{
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving settings: %s", esp_err_to_name(err));
        return err;
    }
    if (s_settings_base[0] == '\0') { /* a deep-sleep wake doesn't read the file: read it now */
        bool from_backup = false;
        if (storage_load(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), keep_text, NULL,
                         &from_backup) != ESP_OK) {
            s_settings_base[0] = '\0';
        }
    }
    static char out[sizeof(s_settings_base)];
    size_t n = settings_to_json(&s.settings, s_settings_base[0] ? s_settings_base : NULL, out, sizeof(out));
    if (n == 0) {
        ESP_LOGE(TAG, "settings.json does not fit %u bytes", (unsigned)sizeof(out));
        return ESP_ERR_INVALID_SIZE;
    }
    err = storage_write_atomic(STORAGE_SETTINGS_PATH, out, n);
    if (err == ESP_OK) {
        memcpy(s_settings_base, out, n + 1); /* the next save builds on what is now in the file */
    } else {
        ESP_LOGE(TAG, "saving settings: %s", esp_err_to_name(err));
    }
    return err;
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
        app_ui_save_presets();
    }
}

void app_ui_set_cycle(bool enabled)
{
    s.presets.cycle_enabled = enabled;
    s.cycle_at = enabled ? time(NULL) + s.presets.cycle_interval_s : 0;
    ESP_LOGI(TAG, "auto-cycle %s", enabled ? "on" : "off");
    app_ui_save_presets();
}

void app_ui_toggle_cycle(void)
{
    app_ui_set_cycle(!s.presets.cycle_enabled);
}

void app_ui_start_night(time_t until)
{
    s.night_until = until - until % 60;
    ESP_LOGI(TAG, "night until %lld", (long long)s.night_until);
}

void app_ui_end_night(void)
{
    s.sched_checked = ui_schedule_after_night(s.night_until); /* the entries inside it don't run */
    s.night_until = 0;
    ESP_LOGI(TAG, "night over");
}

bool app_ui_night(void)
{
    return s.night_until != 0;
}

static bool schedule_runs(void)
{
    return s.presets.schedule.enabled && s.presets.schedule.count > 0 && timekeeping_valid() && s.night_until == 0 &&
           !s.critical; /* the critical screen stays put, and a night would take its place */
}

/* Runs the entries that came due since the last check, in order; a night entry ends the run, as
 * the entries after it fall inside the night. */
static void run_schedule(time_t now)
{
    if (!schedule_runs()) {
        s.sched_checked = now;
        return;
    }
    /* Checks come at every display slot and at every entry's minute (spec §9.2): a longer gap was a
     * sleep no entry could end, such as the critical one. */
    time_t max_gap = (time_t)s.settings.display_every_min * 60 + 300;
    int order[UI_SCHEDULE_MAX];
    int n = ui_schedule_step(&s.presets.schedule, &s.sched_checked, now, max_gap, order);
    for (int i = 0; i < n; i++) {
        const ui_schedule_entry_t *e = &s.presets.schedule.entries[order[i]];
        if (e->action == UI_SCHED_NIGHT) {
            char until[8], text[48];
            time_t end = sched_next_weekly(now, e->until_min, 0x7F);
            snprintf(until, sizeof(until), "%02d:%02d", e->until_min / 60, e->until_min % 60);
            snprintf(text, sizeof(text), "%s %s", lang_str(lang_get(s.settings.language), LS_T_NIGHT_UNTIL), until);
            app_ui_start_night(end);
            app_ui_toast(text);
            break;
        }
        ESP_LOGI(TAG, "schedule: preset %s", s.presets.presets[e->preset].id);
        app_ui_select(e->preset, false);
    }
}

void app_ui_tick(bool force)
{
    time_t now = time(NULL);
    if (s.night_until != 0 && now < s.night_until && display_asleep()) {
        return; /* night sleep: nothing is sampled or drawn (spec §9.1) */
    }
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
    run_schedule(now);
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
    int index = -1;
    sched_input_t in = {
        .now = now,
        .display_every_min = s.settings.display_every_min,
        .sensors_every_min = s.settings.sensors_every_min,
        .cycle_at = s.presets.cycle_enabled ? s.cycle_at : 0,
        .every_second = s.presets.presets[s.presets.active].seconds,
        .schedule_at = schedule_runs() ? ui_schedule_next(&s.presets.schedule, now, &index) : 0,
    };
    return scheduler_next_wake(&in);
}
