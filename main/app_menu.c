#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "board_buttons.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lang.h"
#include "netmgr.h"
#include "power.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "timekeeping_zones.h"

/* The menu on the device (spec §5.7): builds the model from the app's state, carries out what the
 * menu asks for, and keeps the panel in HPM while it is open (spec §4.2). */

#define MENU_TIMEOUT_MS 60000 /* spec §5.7: the menu closes after 60 s without input */
#define ZONES_MAX       20

static const char *TAG = "app_menu";

_Static_assert(UI_MI_STEP_ENERGY - UI_MI_STEP_WEATHER == 4 && SETTINGS_STEP_ENERGY == 1 << 4,
               "Sync ▸ Steps follows settings_step_t: a switch a bit");

static int64_t s_closed_ms; /* app_uptime_ms() when the menu last closed */

/* Gesture timings (spec §5.6): the dashboard binds both double presses (BOOT's since M6b, D31) and
 * a 3 s BOOT long press; the menu binds neither double, so KEY short answers at once. */
const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = true },
};
static const gesture_config_t k_menu_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = false },
    [BOARD_BUTTON_BOOT] = { .long_ms = 1000, .double_enabled = false },
};

static const uint16_t k_cycle_s[] = { 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
static const char *const k_cycle_labels[] = { "10 s", "15 s", "30 s", "1 min", "2 min",
                                              "5 min", "10 min", "15 min", "30 min", "1 h" };
#define CYCLE_COUNT ((int)(sizeof(k_cycle_s) / sizeof(k_cycle_s[0])))
static const uint8_t k_quarter_hz[] = { 1, 2, 4, 8, 16, 32 }; /* 0.25 to 8 Hz (D12) */
#define RATE_COUNT ((int)(sizeof(k_quarter_hz) / sizeof(k_quarter_hz[0])))
static const char *const k_units[] = { "°C", "°F" };
static const uint16_t k_sync_min[] = { 15, 30, 60, 120, 180, 360, 720, 1440 }; /* spec §9.3: 15-1440 */
static const char *const k_sync_labels[] = { "15 min", "30 min", "1 h", "2 h", "3 h", "6 h", "12 h", "24 h" };
#define SYNC_STEPS ((int)(sizeof(k_sync_min) / sizeof(k_sync_min[0])))
static const char *const k_languages[] = { "en", "cs" };
#define LANGUAGE_COUNT ((int)(sizeof(k_languages) / sizeof(k_languages[0])))

static ui_menu_t s_menu;
static ui_menu_model_t s_model;
static bool s_open;
static int64_t s_deadline_ms;
static const char *s_preset_names[UI_PRESET_MAX];
static const char *s_zone_names[ZONES_MAX];
static const char *s_language_names[LANGUAGE_COUNT];
static char s_rate_text[RATE_COUNT][12];
static const char *s_rates[RATE_COUNT];
static char s_info[8][80];
static const char *s_sync_modes[4];

static const lang_t *lang(void)
{
    return lang_get(app_settings()->language);
}

/* "0,25 Hz", "1 Hz": the pack's decimal separator, no trailing zeros. */
static void rate_label(const lang_t *l, int quarter_hz, char *out, size_t size)
{
    char num[12];
    lang_format_decimal(l, quarter_hz * 25, 2, num, sizeof(num));
    size_t n = strlen(num);
    while (n > 1 && num[n - 1] == '0') {
        num[--n] = '\0';
    }
    if (n > 0 && num[n - 1] == l->decimal_sep) {
        num[--n] = '\0';
    }
    snprintf(out, size, "%s Hz", num);
}

static int nearest_index(const uint16_t *values, int count, int value)
{
    int best = 0;
    for (int i = 0; i < count; i++) {
        if (values[i] <= value) {
            best = i;
        }
    }
    return best;
}

static void format_uptime(const lang_t *l, int64_t s, char *out, size_t size)
{
    if (s >= 86400) {
        snprintf(out, size, "%lld %s %lld %s", (long long)(s / 86400), lang_str(l, LS_DAYS_UNIT),
                 (long long)(s % 86400 / 3600), lang_str(l, LS_HOURS_UNIT));
    } else if (s >= 3600) {
        snprintf(out, size, "%lld %s %lld %s", (long long)(s / 3600), lang_str(l, LS_HOURS_UNIT),
                 (long long)(s % 3600 / 60), lang_str(l, LS_MINUTES_UNIT));
    } else {
        snprintf(out, size, "%lld %s", (long long)(s / 60), lang_str(l, LS_MINUTES_UNIT));
    }
}

static int zone_list(int *current)
{
    const settings_t *set = app_settings();
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    for (int i = 0; i < count && i < ZONES_MAX; i++) {
        s_zone_names[i] = zones[i].iana;
    }
    *current = timekeeping_zone_find(set->tz_iana);
    if (*current < 0 && count < ZONES_MAX) { /* a zone set elsewhere (the web UI, M4) stays selectable */
        s_zone_names[count] = set->tz_iana;
        *current = count++;
    }
    return count;
}

static void build_model(void)
{
    const app_ui_state_t *st = app_state();
    const settings_t *set = &st->settings;
    const lang_t *l = lang();
    ui_menu_model_t *m = &s_model;
    memset(m, 0, sizeof(*m));

    for (int i = 0; i < st->presets.count; i++) {
        s_preset_names[i] = st->presets.presets[i].name;
    }
    m->choices[UI_MI_ACTIVE_PRESET] = s_preset_names;
    m->choice_count[UI_MI_ACTIVE_PRESET] = st->presets.count;
    m->value[UI_MI_ACTIVE_PRESET] = st->presets.active;
    m->value[UI_MI_AUTO_CYCLE] = st->presets.cycle_enabled;
    m->choices[UI_MI_CYCLE_INTERVAL] = k_cycle_labels;
    m->choice_count[UI_MI_CYCLE_INTERVAL] = CYCLE_COUNT;
    m->value[UI_MI_CYCLE_INTERVAL] = nearest_index(k_cycle_s, CYCLE_COUNT, st->presets.cycle_interval_s);
    m->value[UI_MI_SCHEDULE] = st->presets.schedule.enabled;

    time_t now = time(NULL);
    localtime_r(&now, &m->local);
    m->value[UI_MI_CLOCK_24H] = set->clock_24h;
    int zone = -1;
    m->choice_count[UI_MI_TIME_ZONE] = (uint8_t)zone_list(&zone);
    m->choices[UI_MI_TIME_ZONE] = s_zone_names;
    m->value[UI_MI_TIME_ZONE] = zone;

    static const lang_str_t k_modes[4] = { LS_SYNC_TIMES, LS_SYNC_INTERVAL, LS_SYNC_ALWAYS, LS_SYNC_MANUAL };
    for (int i = 0; i < 4; i++) {
        s_sync_modes[i] = lang_str(l, k_modes[i]);
    }
    m->choices[UI_MI_SYNC_MODE] = s_sync_modes;
    m->choice_count[UI_MI_SYNC_MODE] = 4;
    m->value[UI_MI_SYNC_MODE] = set->sync_mode;
    m->choices[UI_MI_SYNC_INTERVAL] = k_sync_labels;
    m->choice_count[UI_MI_SYNC_INTERVAL] = SYNC_STEPS;
    m->value[UI_MI_SYNC_INTERVAL] = nearest_index(k_sync_min, SYNC_STEPS, set->sync_interval_min);
    m->hidden[UI_MI_SYNC_INTERVAL] = set->sync_mode != SETTINGS_SYNC_INTERVAL;
    m->value[UI_MI_QUIET_HOURS] = set->quiet;
    for (int i = 0; i < 5; i++) { /* Sync ▸ Steps (D35), in settings_step_t order */
        m->value[UI_MI_STEP_WEATHER + i] = (set->sync_steps >> i) & 1;
    }

    m->value[UI_MI_UPDATE_INTERVAL] = set->display_every_min;
    int rate = 0;
    for (int i = 0; i < RATE_COUNT; i++) {
        rate_label(l, k_quarter_hz[i], s_rate_text[i], sizeof(s_rate_text[i]));
        s_rates[i] = s_rate_text[i];
        if (k_quarter_hz[i] <= set->lpm_quarter_hz) {
            rate = i;
        }
    }
    m->choices[UI_MI_REFRESH_RATE] = s_rates;
    m->choice_count[UI_MI_REFRESH_RATE] = RATE_COUNT;
    m->value[UI_MI_REFRESH_RATE] = rate;

    m->value[UI_MI_TEMP_OFFSET] = set->temp_offset_c100 / 10;
    m->value[UI_MI_HUM_OFFSET] = set->hum_offset_pct100 / 10;
    m->choices[UI_MI_UNITS] = k_units;
    m->choice_count[UI_MI_UNITS] = 2;
    m->value[UI_MI_UNITS] = set->fahrenheit;

    for (int i = 0; i < LANGUAGE_COUNT; i++) {
        s_language_names[i] = lang_get(k_languages[i])->name;
        if (strcmp(set->language, k_languages[i]) == 0) {
            m->value[UI_MI_LANGUAGE] = i;
        }
    }
    m->choices[UI_MI_LANGUAGE] = s_language_names;
    m->choice_count[UI_MI_LANGUAGE] = LANGUAGE_COUNT;

    ui_context_t ctx;
    app_ui_context(&ctx);
    ui_value_t bat;
    ui_resolve(&ctx, UI_FIELD_BAT_LEVEL, &bat);
    if (bat.state == UI_VALUE_MISSING) {
        snprintf(s_info[0], sizeof(s_info[0]), "\xE2\x80\x94");
    } else {
        snprintf(s_info[0], sizeof(s_info[0]), "%s %% \xC2\xB7 %s", bat.text, bat.extra);
    }
    char sha[8];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    snprintf(s_info[1], sizeof(s_info[1]), "%.24s (%s)", esp_app_get_description()->version, sha);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA); /* the device id (spec §5.5) */
    snprintf(s_info[2], sizeof(s_info[2]), "reflbo-%02x%02x", mac[4], mac[5]);
    if (st->cold_boot_at != 0 && timekeeping_valid() && now >= st->cold_boot_at) {
        format_uptime(l, now - st->cold_boot_at, s_info[3], sizeof(s_info[3]));
    } else {
        snprintf(s_info[3], sizeof(s_info[3]), "\xE2\x80\x94");
    }
    char mb[12];
    lang_format_decimal(l, (long)(esp_get_free_heap_size() / (1024 * 1024 / 10)), 1, mb, sizeof(mb));
    snprintf(s_info[4], sizeof(s_info[4]), "%s MB", mb);
    netmgr_status_t net = { 0 };
    if (app_net_ready()) {
        netmgr_status(&net); /* off: no address */
    }
    snprintf(s_info[5], sizeof(s_info[5]), "%s",
             net.ip[0] ? net.ip : net.state == NETMGR_AP ? NETMGR_AP_IP : "\xE2\x80\x94");
    snprintf(s_info[6], sizeof(s_info[6]), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    app_sync_summary(s_info[7], sizeof(s_info[7]));
    const ui_menu_item_t info_items[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE,
                                          UI_MI_INFO_UPTIME,  UI_MI_INFO_MEMORY,   UI_MI_INFO_IP,
                                          UI_MI_INFO_MAC,     UI_MI_INFO_SYNC };
    for (int i = 0; i < 8; i++) {
        m->info[info_items[i]] = s_info[i];
    }
}

static void set_zone(int index)
{
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    if (index < 0 || index >= count) {
        return; /* the zone set elsewhere: unchanged */
    }
    settings_t *set = &app_state()->settings;
    snprintf(set->tz_iana, sizeof(set->tz_iana), "%s", zones[index].iana);
    snprintf(set->tz_posix, sizeof(set->tz_posix), "%s", zones[index].posix);
}

static void apply(const ui_menu_intent_t *in)
{
    app_ui_state_t *st = app_state();
    settings_t *set = &st->settings;
    bool save_settings = false, save_presets = false, apply_settings = false, resample = false, retime = false;
    switch (in->kind) {
    case UI_MENU_SET:
        switch (in->item) {
        case UI_MI_ACTIVE_PRESET:
            app_ui_select(in->value, true);
            break;
        case UI_MI_AUTO_CYCLE:
            app_ui_set_cycle(in->value != 0);
            break;
        case UI_MI_CYCLE_INTERVAL:
            st->presets.cycle_interval_s = k_cycle_s[in->value < CYCLE_COUNT ? in->value : 0];
            if (st->presets.cycle_enabled) {
                st->cycle_at = time(NULL) + st->presets.cycle_interval_s;
            }
            save_presets = true;
            break;
        case UI_MI_SCHEDULE:
            st->presets.schedule.enabled = in->value != 0;
            st->sched_checked = time(NULL);
            save_presets = true;
            break;
        case UI_MI_CLOCK_24H:
            set->clock_24h = in->value != 0;
            save_settings = true;
            break;
        case UI_MI_TIME_ZONE:
            set_zone(in->value);
            apply_settings = save_settings = retime = true;
            break;
        case UI_MI_UPDATE_INTERVAL:
            set->display_every_min = (uint8_t)in->value;
            save_settings = retime = true;
            break;
        case UI_MI_SYNC_MODE: {
            uint8_t prev = set->sync_mode;
            set->sync_mode = (uint8_t)(in->value >= 0 && in->value <= SETTINGS_SYNC_MANUAL ? in->value : 0);
            settings_remember_mode(set, prev); /* BOOT double returns to it (D31) */
            save_settings = retime = true;     /* retime schedules the next sync */
            break;
        }
        case UI_MI_SYNC_INTERVAL:
            set->sync_interval_min = k_sync_min[in->value >= 0 && in->value < SYNC_STEPS ? in->value : 2];
            save_settings = retime = true;
            break;
        case UI_MI_QUIET_HOURS:
            set->quiet = in->value != 0;
            save_settings = retime = true;
            break;
        case UI_MI_STEP_WEATHER:
        case UI_MI_STEP_AIR:
        case UI_MI_STEP_RADAR:
        case UI_MI_STEP_SOLAR:
        case UI_MI_STEP_ENERGY: { /* a step that is off makes no requests, its refreshes in `always` included */
            uint8_t bit = (uint8_t)(1u << (in->item - UI_MI_STEP_WEATHER));
            set->sync_steps = (uint8_t)(in->value ? set->sync_steps | bit : set->sync_steps & ~bit);
            save_settings = true;
            break;
        }
        case UI_MI_REFRESH_RATE:
            set->lpm_quarter_hz = k_quarter_hz[in->value < RATE_COUNT ? in->value : 2];
            apply_settings = save_settings = true;
            break;
        case UI_MI_TEMP_OFFSET:
            set->temp_offset_c100 = (int16_t)(in->value * 10);
            apply_settings = save_settings = resample = true;
            break;
        case UI_MI_HUM_OFFSET:
            set->hum_offset_pct100 = (int16_t)(in->value * 10);
            apply_settings = save_settings = resample = true;
            break;
        case UI_MI_UNITS:
            set->fahrenheit = in->value == 1;
            save_settings = true;
            break;
        case UI_MI_LANGUAGE:
            snprintf(set->language, sizeof(set->language), "%s",
                     k_languages[in->value < LANGUAGE_COUNT ? in->value : 0]);
            save_settings = true;
            break;
        default:
            break;
        }
        break;
    case UI_MENU_SET_TIME: {
        time_t before = time(NULL);
        time_t utc = sched_local_to_utc(in->local.tm_year + 1900, in->local.tm_mon + 1, in->local.tm_mday,
                                        in->local.tm_hour * 60 + in->local.tm_min);
        esp_err_t err = timekeeping_set_utc(utc);
        if (err == ESP_OK) {
            app_clock_moved((int64_t)utc - (int64_t)before);
        } else {
            ESP_LOGE(TAG, "setting the time: %s", esp_err_to_name(err));
        }
        break;
    }
    case UI_MENU_ACTION:
        switch (in->item) {
        case UI_MI_CONFIG_MODE:
            app_config_enter(); /* closes the menu */
            return;
        case UI_MI_FORGET_NETWORKS:
            if (app_net_init() == ESP_OK && netmgr_forget_all() == ESP_OK) {
                app_menu_close();
                app_ui_toast(lang_str(lang(), LS_T_NETWORKS_FORGOTTEN));
            }
            return;
        case UI_MI_RESET_PASSWORD:
            if (webui_reset_password() == ESP_OK) {
                app_menu_close();
                app_ui_toast(lang_str(lang(), LS_T_PASSWORD_CLEARED));
            }
            return;
        case UI_MI_SYNC_NOW:
            app_menu_close();
            app_sync_now_toast();
            return;
        case UI_MI_REBOOT:
            app_restart(LS_T_REBOOTING, false);
            break;
        case UI_MI_FACTORY_RESET:
            app_factory_reset();
            break;
        default:
            break;
        }
        break;
    case UI_MENU_CLOSE:
        app_menu_close();
        return;
    default:
        break;
    }
    if (apply_settings) {
        app_ui_apply_settings();
    }
    if (resample) {
        app_ui_sample(time(NULL));
    }
    if (save_settings) {
        app_ui_save_settings();
    }
    if (save_presets) {
        app_ui_save_presets();
    }
    if (retime) {
        app_clock_moved(0); /* new slots or a new zone: schedule the wakes again */
    }
}

void app_menu_open(void)
{
    if (s_open) {
        return;
    }
    ui_menu_open(&s_menu);
    s_open = true;
    s_deadline_ms = app_uptime_ms() + MENU_TIMEOUT_MS;
    power_hold_awake_ms(MENU_TIMEOUT_MS);
    board_buttons_set_config(k_menu_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_HPM); /* new frames appear at once (spec §4.2) */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "menu open");
    app_menu_render();
}

void app_menu_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    s_closed_ms = app_uptime_ms();
    board_buttons_set_config(k_app_dashboard_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "menu closed");
    app_ui_render();
}

bool app_menu_is_open(void)
{
    return s_open;
}

int64_t app_menu_deadline_ms(void)
{
    return s_open ? s_deadline_ms : 0;
}

bool app_menu_closed_within(int64_t ms)
{
    return s_closed_ms != 0 && app_uptime_ms() - s_closed_ms < ms;
}

void app_menu_key(ui_menu_key_t key)
{
    if (!s_open) {
        return;
    }
    s_deadline_ms = app_uptime_ms() + MENU_TIMEOUT_MS;
    power_hold_awake_ms(MENU_TIMEOUT_MS);
    build_model();
    ui_menu_intent_t in = ui_menu_input(&s_menu, &s_model, key);
    apply(&in);
    if (s_open) {
        app_menu_render();
    }
}

void app_menu_render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    build_model();
    ui_draw_menu(fb, &s_menu, &s_model, lang());
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}
