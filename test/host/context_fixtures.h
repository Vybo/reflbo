#pragma once

#include "datastore.h"
#include "lang.h"
#include "ui_fields.h"
#include "util_time.h"

/* Fixed inputs for the ui tests (test_ui_fields.c, dashboard_fixtures.h): Friday 25 September
 * 2026, 20:48 CEST (18:48 UTC). */

#define FIX_DAY ((int32_t)20721) /* util_days_from_civil(2026, 9, 25) */
#define FIX_NOW ((time_t)FIX_DAY * 86400 + 18 * 3600 + 48 * 60)

static ds_t s_fix_ds;

static inline struct tm fixture_local(int hour, int minute, int second)
{
    struct tm tm = { .tm_year = 126, .tm_mon = 8, .tm_mday = 25, .tm_wday = 5, .tm_hour = hour,
                     .tm_min = minute, .tm_sec = second, .tm_yday = 267 };
    return tm;
}

/* An hour of readings, warming 0.7 °C, then battery and days left: everything fresh. */
static inline void fixture_fill(ds_t *ds, time_t now)
{
    ds_init(ds);
    for (int i = 0; i <= 12; i++) {
        time_t t = now - 3600 + 300 * i;
        int temp = 2270 + 6 * i - (i == 3 ? 250 : 0); /* one cold reading makes the day's low */
        ds_set_env(ds, i == 12 ? 2340 : temp, 4500, t, FIX_DAY);
    }
    ds_set_env(ds, 2410, 4500, now - 1200, FIX_DAY); /* the day's high, then back down */
    ds_set_env(ds, 2340, 4500, now, FIX_DAY);
    ds_set_battery(ds, 87, 3921, DS_BAT_DISCHARGING, now);
    ds_set(ds, DS_BAT_DAYS, 85, now);
}

static inline ui_context_t fixture_context(void)
{
    fixture_fill(&s_fix_ds, FIX_NOW);
    ui_context_t ctx = { .now = FIX_NOW, .local = fixture_local(20, 48, 0), .time_valid = true,
                         .local_day = FIX_DAY, .ds = &s_fix_ds, .lang = lang_get("en"), .clock_24h = true };
    return ctx;
}
