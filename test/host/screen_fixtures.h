#pragma once

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "timekeeping_zones.h"
#include "ui_menu.h"
#include "ui_screens.h"

/* Fixed inputs for the menu and the special screens (test_ui_screens_golden.c, render_screen.c),
 * on top of the dashboard fixtures. */

static const char *const k_fix_presets[] = { "Home", "Indoor", "Weather", "Focus clock" };
static const char *const k_fix_intervals[] = { "10 s", "15 s", "30 s", "1 min", "2 min",
                                               "5 min", "10 min", "15 min", "30 min", "1 h" };
static const char *const k_fix_units[] = { "°C", "°F" };
static const char *const k_fix_languages[] = { "English", "Čeština" };
static const char *s_fix_zones[16];
static char s_fix_rate_text[6][12];
static const char *s_fix_rates[6];

/* The model the app would build: Indoor active, Prague, 1 Hz, a -1.5 °C offset. */
static inline void fixture_menu_model(ui_menu_model_t *m, const lang_t *lang)
{
    memset(m, 0, sizeof(*m));
    int zones = 0;
    const timekeeping_zone_t *z = timekeeping_zones(&zones);
    for (int i = 0; i < zones && i < 16; i++) {
        s_fix_zones[i] = z[i].iana;
    }
    static const int k_quarter_hz[] = { 1, 2, 4, 8, 16, 32 };
    for (int i = 0; i < 6; i++) {
        char num[8];
        lang_format_decimal(lang, k_quarter_hz[i] * 25, 2, num, sizeof(num));
        size_t n = strlen(num);
        while (n > 1 && num[n - 1] == '0') { /* 0.25, 0.5, 1 */
            num[--n] = '\0';
        }
        if (num[n - 1] == lang->decimal_sep) {
            num[n - 1] = '\0';
        }
        snprintf(s_fix_rate_text[i], sizeof(s_fix_rate_text[i]), "%s Hz", num);
        s_fix_rates[i] = s_fix_rate_text[i];
    }
    m->choices[UI_MI_ACTIVE_PRESET] = k_fix_presets;
    m->choice_count[UI_MI_ACTIVE_PRESET] = 4;
    m->value[UI_MI_ACTIVE_PRESET] = 1;
    m->choices[UI_MI_CYCLE_INTERVAL] = k_fix_intervals;
    m->choice_count[UI_MI_CYCLE_INTERVAL] = 10;
    m->value[UI_MI_CYCLE_INTERVAL] = 3;
    m->value[UI_MI_CLOCK_24H] = 1;
    m->choices[UI_MI_TIME_ZONE] = s_fix_zones;
    m->choice_count[UI_MI_TIME_ZONE] = (uint8_t)zones;
    m->value[UI_MI_TIME_ZONE] = 2;
    m->value[UI_MI_UPDATE_INTERVAL] = 1;
    m->choices[UI_MI_REFRESH_RATE] = s_fix_rates;
    m->choice_count[UI_MI_REFRESH_RATE] = 6;
    m->value[UI_MI_REFRESH_RATE] = 2;
    m->value[UI_MI_TEMP_OFFSET] = -15;
    m->choices[UI_MI_UNITS] = k_fix_units;
    m->choice_count[UI_MI_UNITS] = 2;
    m->choices[UI_MI_LANGUAGE] = k_fix_languages;
    m->choice_count[UI_MI_LANGUAGE] = 2;
    m->value[UI_MI_LANGUAGE] = strcmp(lang->code, "cs") == 0;
    m->info[UI_MI_INFO_BATTERY] = "82 % · 4.04 V";
    m->info[UI_MI_INFO_FIRMWARE] = "0.3.0 (0b21fcd)";
    m->info[UI_MI_INFO_DEVICE] = "reflbo-bb94";
    m->info[UI_MI_INFO_IP] = "192.168.1.57";
    m->info[UI_MI_INFO_MAC] = "14:c1:9f:54:bb:94";
    m->info[UI_MI_INFO_UPTIME] = "2 d 3 h";
    m->info[UI_MI_INFO_MEMORY] = "7.9 MB";
    m->local = fixture_local(20, 48, 0);
}

/* Moves the menu's cursor to `item` in the current section. */
static inline void fixture_menu_to(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item)
{
    for (int i = 0; i < UI_MI_COUNT && ui_menu_current(m, model) != item; i++) {
        ui_menu_input(m, model, UI_MENU_KEY_NEXT);
    }
}

static inline void fixture_menu_open(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t section,
                                     ui_menu_item_t item)
{
    ui_menu_open(m);
    if (section != UI_MI_ROOT) {
        fixture_menu_to(m, model, section);
        ui_menu_input(m, model, UI_MENU_KEY_SELECT);
    }
    fixture_menu_to(m, model, item);
}

/* Config mode (spec §10.2) in each Wi-Fi state; `name` picks the state and the QR code. */
static inline bool fixture_config(const char *name, ui_config_view_t *v)
{
    *v = (ui_config_view_t){ .state = UI_NET_AP, .ap_on = true, .ssid = "", .ip = "", .host = "reflbo-bb94",
                             .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
    if (strncmp(name, "config_ap_url", 13) == 0) {
        v->qr_url = true;
    } else if (strncmp(name, "config_station_back", 19) == 0) { /* KEY brought it back over the dashboard (D20) */
        v->state = UI_NET_STATION;
        v->ssid = "Vybiral Home 5G";
        v->ip = "192.168.1.57";
        v->ap_on = false; /* a password is set, so the AP doesn't run beside the station */
        v->back = true;
    } else if (strncmp(name, "config_starting", 15) == 0) {
        *v = (ui_config_view_t){ .state = UI_NET_STARTING, .ssid = "", .ip = "", .host = "reflbo-bb94",
                                 .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
    } else if (strncmp(name, "config_joining", 14) == 0) {
        v->state = UI_NET_JOINING;
        v->ap_on = false;
        v->ssid = "Vybiral Home 5G";
    } else if (strncmp(name, "config_station_ap", 17) == 0) { /* no web password yet (D18) */
        v->state = UI_NET_STATION;
        v->ssid = "Vybiral Home 5G";
        v->ip = "192.168.1.57";
        v->minutes_left = 7;
    } else if (strncmp(name, "config_station", 14) == 0) {
        v->state = UI_NET_STATION;
        v->ap_on = false;
        v->ssid = "Vybiral Home 5G";
        v->ip = "192.168.1.57";
        v->minutes_left = 3;
    } else if (strncmp(name, "config_ap", 9) != 0) {
        return false;
    }
    return true;
}

/* Draws screen fixture `name` into fb; false for an unknown name. */
static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
{
    static ui_menu_model_t model;
    static ui_menu_t menu;
    const lang_t *lang = lang_get(strstr(name, "_cs") != NULL ? "cs" : "en");
    fixture_menu_model(&model, lang);
    if (strncmp(name, "menu_root", 9) == 0) {
        fixture_menu_open(&menu, &model, UI_MI_ROOT, UI_MI_PRESETS);
    } else if (strcmp(name, "menu_presets_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_PRESETS, UI_MI_ACTIVE_PRESET);
    } else if (strcmp(name, "menu_edit_zone_en") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_TIME, UI_MI_TIME_ZONE);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
        ui_menu_input(&menu, &model, UI_MENU_KEY_BACK); /* Prague -> London */
    } else if (strcmp(name, "menu_edit_offset_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SENSORS, UI_MI_TEMP_OFFSET);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
    } else if (strcmp(name, "menu_info_en") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_INFO, UI_MI_INFO_BATTERY);
    } else if (strcmp(name, "menu_system_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SYSTEM, UI_MI_LANGUAGE);
    } else if (strcmp(name, "menu_datetime_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_TIME, UI_MI_SET_DATETIME);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT); /* on to the month */
    } else if (strcmp(name, "menu_confirm_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SYSTEM, UI_MI_FACTORY_RESET);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
    } else if (strcmp(name, "menu_wifi_en") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_WIFI, UI_MI_CONFIG_MODE);
    } else if (strcmp(name, "menu_confirm_password_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_WIFI, UI_MI_RESET_PASSWORD);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
    } else if (strncmp(name, "config_", 7) == 0) {
        ui_config_view_t v;
        if (!fixture_config(name, &v)) {
            return false;
        }
        ui_draw_config(fb, &v, lang);
        return true;
    } else if (strncmp(name, "first_run", 9) == 0) {
        ui_context_t ctx = fixture_context();
        ctx.lang = lang;
        ctx.time_valid = strstr(name, "invalid") == NULL;
        ui_draw_first_run(fb, &ctx);
        return true;
    } else if (strcmp(name, "toast_preset_cs") == 0) {
        ui_context_t ctx;
        ui_preset_t preset;
        fixture_dashboard("home_cs", &ctx, &preset);
        ui_draw_dashboard(fb, &ctx, &preset);
        char text[64];
        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), "Indoor");
        ui_draw_toast(fb, text);
        return true;
    } else if (strncmp(name, "message", 7) == 0) { /* M7: Home Assistant's message over the dashboard */
        ui_context_t ctx;
        ui_preset_t preset;
        bool inverted = strstr(name, "inverted") != NULL;
        fixture_dashboard(inverted ? "home_inverted" : lang == lang_get("cs") ? "home_cs" : "home", &ctx, &preset);
        fixture_mqtt(&ctx);
        ctx.lang = lang;
        ha_store_set_message(&s_fix_mqtt, strstr(name, "short") ? "Door open" :
                             lang == lang_get("cs") ? FIX_MESSAGE_CS : FIX_MESSAGE_EN, FIX_NOW - 60);
        ui_draw_dashboard(fb, &ctx, &preset);
        ui_draw_message_banner(fb, &ctx);
        return true;
    } else if (strncmp(name, "critical", 8) == 0) {
        ui_context_t ctx = fixture_context();
        ctx.lang = lang;
        ui_draw_critical(fb, &ctx);
        return true;
    } else {
        return false;
    }
    ui_draw_menu(fb, &menu, &model, lang);
    return true;
}

static const char *const k_screen_fixtures[] = { "menu_root_en", "menu_root_cs", "menu_presets_cs",
                                                 "menu_edit_zone_en", "menu_edit_offset_cs", "menu_info_en",
                                                 "menu_system_cs", "menu_datetime_cs", "menu_confirm_cs",
                                                 "toast_preset_cs", "critical_en", "critical_cs", "menu_wifi_en",
                                                 "menu_confirm_password_cs", "config_ap_en", "config_ap_url_cs",
                                                 "config_starting_en", "config_joining_en", "config_station_en",
                                                 "config_station_ap_cs", "config_station_back_en", "config_station_back_cs",
                                                 "first_run_en", "first_run_invalid_cs", "message_short_en",
                                                 "message_long_en", "message_long_cs", "message_inverted_en" };
