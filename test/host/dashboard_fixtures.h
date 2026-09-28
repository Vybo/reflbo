#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "ui_dashboard.h"

/* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
 * a context and a preset each, on top of context_fixtures.h. */

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
    } else {
        return false;
    }
    return true;
}

static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather", "focus", "home_invalid",
                                                    "home_stale", "home_12h_charging", "indoor_cold",
                                                    "focus_seconds", "home_battery_details", "home_inverted" };
