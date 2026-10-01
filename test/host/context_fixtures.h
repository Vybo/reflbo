#pragma once

#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* A forecast for Brno fetched at `fetched` (UTC): the day's hours from local midnight, cooling into
 * the evening; partly cloudy now, light rain today; a moderate air quality index and grass pollen. */
static inline void fixture_forecast(ds_t *ds, time_t fetched)
{
    static ds_weather_t w;
    static ds_air_t a;
    memset(&w, 0, sizeof(w));
    w.fetched = (uint32_t)fetched;
    w.now_time = (uint32_t)(FIX_NOW - 20 * 60);
    w.now_temp_c10 = 142;
    w.now_feels_c10 = 121;
    w.now_wind_kmh10 = 115;
    w.now_hum = 71;
    w.now_code = 2;
    w.now_is_day = 0;
    w.hour0 = (uint32_t)(FIX_DAY * 86400 - 2 * 3600); /* 00:00 CEST */
    static const uint8_t k_codes[24] = { 0, 0, 1, 1, 2, 3, 3, 61, 61, 63, 61, 80, 80, 3, 3, 2, 2, 1, 1, 0, 2, 2, 3, 3 };
    for (int i = 0; i < DS_WX_HOURS; i++) {
        int hour = i % 24;
        int temp = 90 + (hour < 15 ? hour * 7 : (24 - hour) * 9) - (i / 24) * 15;
        w.hours[i] = (ds_wx_hour_t){ .temp_c10 = (int16_t)temp, .code = k_codes[hour], .precip = (uint8_t)(hour * 3) };
    }
    w.day0_local = FIX_DAY;
    w.days[0] = (ds_wx_day_t){ .min_c10 = 88, .max_c10 = 183, .code = 61, .precip = 60 };
    w.days[1] = (ds_wx_day_t){ .min_c10 = 61, .max_c10 = 157, .code = 3, .precip = 20 };
    w.days[2] = (ds_wx_day_t){ .min_c10 = 32, .max_c10 = 171, .code = 0, .precip = 0 };
    ds_set_weather(ds, &w);
    memset(&a, 0, sizeof(a));
    a.fetched = (uint32_t)fetched;
    a.hour0 = w.hour0;
    for (int i = 0; i < DS_WX_HOURS; i++) {
        a.aqi[i] = (uint8_t)(28 + (i % 24) / 2);
        a.pm25[i] = (uint8_t)(6 + (i % 24) / 4);
        a.pm10[i] = (uint8_t)(11 + (i % 24) / 3);
        int hour = i % 24; /* the UV index peaks at 13:00 */
        a.uv10[i] = (uint8_t)(hour >= 7 && hour <= 19 ? 45 - 6 * (hour > 13 ? hour - 13 : 13 - hour) : 0);
    }
    a.day0_local = FIX_DAY;
    static const uint16_t k_pollen[DS_POLLEN_TYPES] = { 0, 0, 60, 43, 0, 13 }; /* 0.1 grains/m³ */
    for (int d = 0; d < DS_WX_DAYS; d++) {
        memcpy(a.pollen[d], k_pollen, sizeof(k_pollen));
    }
    ds_set_air(ds, &a);
    ds_set_forecast_ttl(ds, 26 * 3600); /* a daily sync */
    ds_take_changes(ds);
}

/* The fixtures' zone, Europe/Prague: the forecast's hours and the sun are in local time. The file
 * that includes this one defines _POSIX_C_SOURCE first, for setenv(). */
static inline void fixture_zone(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

static inline ui_context_t fixture_context(void)
{
    fixture_zone();
    fixture_fill(&s_fix_ds, FIX_NOW);
    ui_context_t ctx = { .now = FIX_NOW, .local = fixture_local(20, 48, 0), .time_valid = true,
                         .local_day = FIX_DAY, .ds = &s_fix_ds, .lang = lang_get("en"), .clock_24h = true,
                         .lat_e4 = 491951, .lon_e4 = 166068 };
    return ctx;
}
