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
    } else if (strcmp(name, "toast_preset_cs") == 0) {
        ui_context_t ctx;
        ui_preset_t preset;
        fixture_dashboard("home_cs", &ctx, &preset);
        ui_draw_dashboard(fb, &ctx, &preset);
        char text[64];
        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), "Indoor");
        ui_draw_toast(fb, text);
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
                                                 "toast_preset_cs", "critical_en", "critical_cs" };
