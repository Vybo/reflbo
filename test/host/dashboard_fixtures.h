#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "ui_dashboard.h"

/* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
 * a context and a preset each, on top of context_fixtures.h. */

/* One reading only, so today's low and high equal it; the battery as in fixture_fill. */
static inline void fixture_single(ds_t *ds, int temp_c100, int hum_pct100)
{
    ds_init(ds);
    ds_set_env(ds, temp_c100, hum_pct100, FIX_NOW, FIX_DAY);
    ds_set_battery(ds, 87, 3921, DS_BAT_DISCHARGING, FIX_NOW);
    ds_set(ds, DS_BAT_DAYS, 85, FIX_NOW);
}

static inline ui_preset_t fixture_preset(const char *id)
{
    ui_presets_t all;
    ui_presets_defaults(&all);
    int i = ui_presets_find(&all, id);
    return all.presets[i < 0 ? 0 : i];
}

/* Each fixture: a context and a preset. */
static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
{
    *ctx = fixture_context();
    if (strcmp(name, "home") == 0 || strcmp(name, "indoor") == 0 || strcmp(name, "weather") == 0 ||
        strcmp(name, "focus") == 0) {
        *preset = fixture_preset(name);
    } else if (strcmp(name, "home_invalid") == 0) { /* RTC stopped, no readings yet (spec §5.3) */
        *preset = fixture_preset("home");
        ctx->time_valid = false;
        ds_init(&s_fix_ds);
    } else if (strcmp(name, "home_stale") == 0) { /* readings 3 h old */
        *preset = fixture_preset("home");
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
    } else if (strcmp(name, "home_12h_charging") == 0) {
        *preset = fixture_preset("home");
        ctx->clock_24h = false;
        ds_set_battery(&s_fix_ds, 64, 4012, DS_BAT_CHARGING, FIX_NOW);
    } else if (strcmp(name, "indoor_cold") == 0) { /* below zero, °F, placeholder policy for stale */
        *preset = fixture_preset("indoor");
        ds_init(&s_fix_ds);
        ds_set_env(&s_fix_ds, -50, 8000, FIX_NOW, FIX_DAY);
        ctx->fahrenheit = true;
        preset->stale_policy = UI_STALE_PLACEHOLDER;
    } else if (strcmp(name, "focus_seconds") == 0) {
        *preset = fixture_preset("focus");
        preset->seconds = true;
        ctx->local = fixture_local(20, 48, 37);
    } else if (strcmp(name, "home_battery_details") == 0) { /* level, voltage and days in the status bar */
        *preset = fixture_preset("home");
        preset->status_clock = true;
        preset->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    } else if (strcmp(name, "home_inverted") == 0) {
        *preset = fixture_preset("home");
        preset->invert = true;
    } else if (strcmp(name, "indoor_hot_f") == 0) { /* 38.2 °C in °F: three digits */
        *preset = fixture_preset("indoor");
        fixture_single(&s_fix_ds, 3820, 3000);
        ctx->fahrenheit = true;
    } else if (strcmp(name, "indoor_frost") == 0) { /* -12.5 °C; the dew point is -18.7 °C */
        *preset = fixture_preset("indoor");
        fixture_single(&s_fix_ds, -1250, 6000);
    } else if (strcmp(name, "home_frost") == 0) {
        *preset = fixture_preset("home");
        fixture_single(&s_fix_ds, -1250, 6000);
    } else if (strcmp(name, "home_cs") == 0) { /* the Czech pack */
        *preset = fixture_preset("home");
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "indoor_cs") == 0) {
        *preset = fixture_preset("indoor");
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "home_holiday_cs") == 0) { /* Monday 28 September 2026: Den české státnosti */
        *preset = fixture_preset("home");
        preset->slots[4] = UI_FIELD_DATE_HOLIDAY;
        ctx->lang = lang_get("cs");
        ctx->now = FIX_NOW + 3 * 86400;
        ctx->local.tm_mday = 28;
        ctx->local.tm_wday = 1;
        ctx->local.tm_yday = 270;
        ctx->local_day = FIX_DAY + 3;
        fixture_fill(&s_fix_ds, ctx->now);
    } else if (strcmp(name, "home_low_battery") == 0) { /* 12 %: the status bar marks it */
        *preset = fixture_preset("home");
        ds_set_battery(&s_fix_ds, 12, 3650, DS_BAT_DISCHARGING, FIX_NOW);
    } else if (strcmp(name, "home_web") == 0) { /* config mode with a phone logged in (D20) */
        *preset = fixture_preset("home");
        ctx->web_session = true;
    } else if (strcmp(name, "home_stale_web") == 0) { /* the web mark goes after the stale one */
        *preset = fixture_preset("home");
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
        ctx->web_session = true;
    } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
        *preset = fixture_preset("indoor");
        preset->slots[0] = UI_FIELD_TIME_CLOCK;
        ctx->clock_24h = false;
        ctx->local = fixture_local(12, 58, 0);
    } else {
        return false;
    }
    return true;
}

static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather", "focus", "home_invalid",
                                                    "home_stale", "home_12h_charging", "indoor_cold",
                                                    "focus_seconds", "home_battery_details", "home_inverted",
                                                    "indoor_hot_f", "indoor_frost", "home_frost", "grid_clock_12h",
                                                    "home_cs", "indoor_cs", "home_holiday_cs", "home_low_battery",
                                                    "home_web", "home_stale_web" };
