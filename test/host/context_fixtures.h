#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "datastore.h"
#include "ha_store.h"
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
    /* a shower of five quarter hours from 21:45 CEST on the fixture's day: the entries from 22:00, as
     * each holds the 15 minutes before its time */
    static const uint8_t k_mm10[5] = { 2, 5, 9, 6, 3 }, k_prob[5] = { 40, 60, 80, 70, 55 };
    const time_t shower = (time_t)FIX_DAY * 86400 + 20 * 3600;
    w.rain.t0 = (uint32_t)(fetched - fetched % 900); /* the quarter hour of the fetch, as Open-Meteo sends */
    for (int i = 0; i < DS_RAIN_STEPS; i++) {
        time_t t = (time_t)w.rain.t0 + 900 * i;
        int k = t >= shower ? (int)((t - shower) / 900) : -1;
        w.rain.mm10[i] = k >= 0 && k < 5 ? k_mm10[k] : 0;
        w.rain.prob[i] = k >= 0 && k < 5 ? k_prob[k] : 10;
    }
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

/* The stored forecast with every temperature in it moved, the current one to `now_c10`: a frosty
 * morning or a hot day from the same fixture. */
static inline void fixture_forecast_shift(ds_t *ds, int now_c10)
{
    static ds_weather_t w;
    w = *ds_weather(ds);
    int shift = now_c10 - w.now_temp_c10;
    w.now_temp_c10 = (int16_t)now_c10;
    w.now_feels_c10 = (int16_t)(w.now_feels_c10 + shift);
    for (int i = 0; i < DS_WX_HOURS; i++) {
        w.hours[i].temp_c10 = (int16_t)(w.hours[i].temp_c10 + shift);
    }
    for (int d = 0; d < DS_WX_DAYS; d++) {
        w.days[d].min_c10 = (int16_t)(w.days[d].min_c10 + shift);
        w.days[d].max_c10 = (int16_t)(w.days[d].max_c10 + shift);
    }
    ds_set_weather(ds, &w);
}

/* The stored forecast's rain changed: raining at 20:45 CEST (1.2 mm/h), easing off, one unlikely
 * quarter hour; or dry all day. */
static inline void fixture_rain_now(ds_t *ds)
{
    static ds_weather_t w;
    w = *ds_weather(ds);
    static const uint8_t k_mm10[4] = { 3, 4, 2, 1 }, k_prob[4] = { 90, 85, 45, 30 };
    int i0 = ds_rain_index(w.rain.t0, FIX_NOW);
    for (int i = 0; i < DS_RAIN_STEPS; i++) {
        bool rain = i >= i0 && i < i0 + 4;
        w.rain.mm10[i] = rain ? k_mm10[i - i0] : 0;
        w.rain.prob[i] = rain ? k_prob[i - i0] : 5;
    }
    ds_set_weather(ds, &w);
}

static inline void fixture_rain_dry(ds_t *ds)
{
    static ds_weather_t w;
    w = *ds_weather(ds);
    memset(w.rain.mm10, 0, sizeof(w.rain.mm10));
    memset(w.rain.prob, 0, sizeof(w.rain.prob));
    ds_set_weather(ds, &w);
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

/* MQTT fields (spec §12.5): the store's five mappings, and the six keys the presets name. outdoor
 * 21.5 °C and co2 612 ppm ten minutes old, door "Closed" (a text), power 1.24 kW three hours old against
 * its hour (stale), washer mapped but without a value yet; window is named by the presets and mapped by
 * nothing, so its slot stays empty. FIX_MQTT(k) is the field of the presets' key k. */
static ha_store_t s_fix_mqtt;
static ui_mqtt_keys_t s_fix_keys;
#define FIX_MQTT(k) ((ui_field_id_t)(UI_FIELD_MQTT + (k)))

static inline void fixture_mqtt(ui_context_t *ctx)
{
    static const struct {
        const char *key, *label, *unit;
        uint8_t kind;
        uint32_t ttl_s;
    } k_map[] = { { "outdoor", "Outside", "\xC2\xB0" "C", HA_KIND_NUMBER, 0 },
                  { "co2", "CO2", "ppm", HA_KIND_NUMBER, 0 },
                  { "door", "Front door", "", HA_KIND_TEXT, 0 },
                  { "power", "Power", "kW", HA_KIND_NUMBER, 3600 },
                  { "washer", "Washer", "", HA_KIND_TEXT, 0 } };
    static ha_fields_t f;
    memset(&f, 0, sizeof(f));
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        ha_field_t *m = &f.field[f.count++];
        snprintf(m->key, sizeof(m->key), "%s", k_map[i].key);
        snprintf(m->label, sizeof(m->label), "%s", k_map[i].label);
        snprintf(m->unit, sizeof(m->unit), "%s", k_map[i].unit);
        m->kind = k_map[i].kind;
        m->ttl_s = k_map[i].ttl_s;
    }
    ha_store_init(&s_fix_mqtt);
    ha_store_rebuild(&s_fix_mqtt, &f);
    ha_store_set_default_ttl(&s_fix_mqtt, 2 * 86400);
    ha_value_t v = { .kind = HA_KIND_NUMBER, .number = 215, .decimals = 1 };
    ha_store_set(&s_fix_mqtt, 0, &v, FIX_NOW - 600);
    v = (ha_value_t){ .kind = HA_KIND_NUMBER, .number = 612 };
    ha_store_set(&s_fix_mqtt, 1, &v, FIX_NOW - 600);
    v = (ha_value_t){ .kind = HA_KIND_TEXT, .text = "Closed" };
    ha_store_set(&s_fix_mqtt, 2, &v, FIX_NOW - 600);
    v = (ha_value_t){ .kind = HA_KIND_NUMBER, .number = 124, .decimals = 2 };
    ha_store_set(&s_fix_mqtt, 3, &v, FIX_NOW - 3 * 3600);
    static const char *const k_keys[] = { "outdoor", "co2", "door", "power", "washer", "window" };
    memset(&s_fix_keys, 0, sizeof(s_fix_keys));
    for (size_t i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++) {
        snprintf(s_fix_keys.key[s_fix_keys.count++], HA_KEY_LEN, "%s", k_keys[i]);
    }
    ctx->mqtt = &s_fix_mqtt;
    ctx->mqtt_keys = &s_fix_keys;
}
