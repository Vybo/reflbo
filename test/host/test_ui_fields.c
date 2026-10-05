#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "context_fixtures.h"
#include "ui_fields.h"
#include "ui_radar.h"
#include "ui_solar.h"
#include "unity.h"

static ui_context_t s_ctx;

void setUp(void)
{
    s_ctx = fixture_context();
}

void tearDown(void) {}

static ui_value_t resolve(ui_field_id_t f)
{
    ui_value_t v;
    ui_resolve(&s_ctx, f, &v);
    return v;
}

static void test_names_map_both_ways(void)
{
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL(f, ui_field_by_name(info->id));
    }
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_field_by_name("env.nope"));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_field_by_name(NULL));
    TEST_ASSERT_NULL(ui_field_info(UI_FIELD_NONE));
    TEST_ASSERT_NULL(ui_field_info(UI_FIELD_COUNT));
}

static void test_temperature_rounds_to_tenths_with_unit_and_trend(void)
{
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("23.4", v.text);
    TEST_ASSERT_EQUAL_STRING("°C", v.unit);
    TEST_ASSERT_EQUAL_INT(1, v.trend); /* +0.7 °C in the last hour, over the 0.5 °C threshold */
    TEST_ASSERT_EQUAL_STRING("Temperature", v.label);
}

static void test_fahrenheit_converts_values(void)
{
    s_ctx.fahrenheit = true;
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL_STRING("74.1", v.text);
    TEST_ASSERT_EQUAL_STRING("°F", v.unit);
}

static void test_humidity_battery_and_days_left(void)
{
    TEST_ASSERT_EQUAL_STRING("45", resolve(UI_FIELD_ENV_HUM).text);
    ui_value_t bat = resolve(UI_FIELD_BAT_LEVEL);
    TEST_ASSERT_EQUAL_STRING("87", bat.text);
    TEST_ASSERT_EQUAL_STRING("3.92 V", bat.extra);
    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, bat.battery);
    ui_value_t days = resolve(UI_FIELD_BAT_DAYS);
    TEST_ASSERT_EQUAL_STRING("8.5", days.text);
    TEST_ASSERT_EQUAL_STRING("d", days.unit);
    ds_set(&s_fix_ds, DS_BAT_DAYS, 123, FIX_NOW);
    TEST_ASSERT_EQUAL_STRING("12", resolve(UI_FIELD_BAT_DAYS).text); /* whole days from 10 on */
}

static void test_clock_fields_follow_the_time_and_language(void)
{
    ui_value_t t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL_STRING("20:48", t.text);
    TEST_ASSERT_EQUAL_STRING("", t.unit);
    s_ctx.clock_24h = false;
    t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL_STRING("8:48", t.text);
    TEST_ASSERT_EQUAL_STRING("PM", t.unit);
    ui_value_t d = resolve(UI_FIELD_DATE_DAY);
    TEST_ASSERT_EQUAL_STRING("Friday 25 September", d.text);
    TEST_ASSERT_EQUAL_STRING("Fri 25 Sep", d.extra);
    TEST_ASSERT_EQUAL_STRING("39", resolve(UI_FIELD_DATE_WEEK).text);
    ui_value_t m = resolve(UI_FIELD_MOON_PHASE);
    TEST_ASSERT_EQUAL_INT(4, m.moon.index); /* full moon on 26 September 2026 */
    TEST_ASSERT_EQUAL_STRING("Full moon", m.text);
    TEST_ASSERT_EQUAL_STRING("Full", m.short_text);
}

static void test_invalid_time_shows_dashes_and_hides_the_date(void)
{
    s_ctx.time_valid = false;
    ui_value_t t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, t.state);
    TEST_ASSERT_EQUAL_STRING("--:--", t.text);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_DAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_WEEK).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_MOON_PHASE).state);
}

static void test_english_has_no_name_days_or_holidays_and_weather_waits_for_a_sync(void)
{
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_NAMEDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_HOLIDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_WX_NOW).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_AQ_INDEX).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_POLLEN_TOP).state);
}

static void test_the_weather_now_and_today_come_from_the_forecast(void)
{
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    ui_value_t v = resolve(UI_FIELD_WX_NOW);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("14", v.text); /* 14.2 °C, whole degrees */
    TEST_ASSERT_EQUAL_STRING("°C", v.unit);
    TEST_ASSERT_EQUAL_STRING("Partly cloudy", v.extra);
    TEST_ASSERT_EQUAL_STRING("Feels like 12°C", v.detail);
    TEST_ASSERT_TRUE(v.night); /* 20:48, and the current block says it is night */
    v = resolve(UI_FIELD_WX_TODAY);
    TEST_ASSERT_EQUAL_STRING("18° / 9°", v.text);
    TEST_ASSERT_EQUAL_STRING("Rain", v.extra);
    TEST_ASSERT_EQUAL_INT(60, v.percent);
    s_ctx.fahrenheit = true;
    TEST_ASSERT_EQUAL_STRING("58", resolve(UI_FIELD_WX_NOW).text); /* 14.2 °C = 57.6 °F */
}

static void test_the_hourly_strip_starts_at_the_next_hour_every_two_hours(void)
{
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    ui_value_t v = resolve(UI_FIELD_WX_HOURLY);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_INT(6, v.series_count);
    TEST_ASSERT_EQUAL_STRING("21", v.series[0].label);
    TEST_ASSERT_EQUAL_STRING("23", v.series[1].label);
    TEST_ASSERT_EQUAL_STRING("07", v.series[5].label);
    TEST_ASSERT_TRUE(v.series[0].night);
    TEST_ASSERT_FALSE(v.series[5].night); /* 07:30 is after sunrise in late September */
    v = resolve(UI_FIELD_WX_DAILY);
    TEST_ASSERT_EQUAL_INT(3, v.series_count);
    TEST_ASSERT_EQUAL_STRING("Fri", v.series[0].label);
    TEST_ASSERT_EQUAL_STRING("18°", v.series[0].temp);
    TEST_ASSERT_EQUAL_STRING("9°", v.series[0].temp2);
    TEST_ASSERT_EQUAL_STRING("Sun", v.series[2].label);
}

static void test_rain_in_the_next_two_hours_says_when(void)
{
    ui_context_t ctx = fixture_context();
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    ui_value_t v;
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL(UI_FK_SERIES, v.kind);
    TEST_ASSERT_EQUAL_INT(UI_RAIN_STEPS, v.series_count);
    TEST_ASSERT_EQUAL_STRING("Rain from 21:45", v.text); /* 20:48 now: the fifth quarter hour */
    TEST_ASSERT_EQUAL_STRING("", v.extra);
    TEST_ASSERT_EQUAL_UINT8(0, v.rain_mm10[0]); /* 20:45-21:00, Open-Meteo's entry at 21:00 */
    TEST_ASSERT_EQUAL_UINT8(2, v.rain_mm10[4]); /* 21:45-22:00 */
    TEST_ASSERT_EQUAL_UINT8(40, v.rain_prob[4]);
    ctx.clock_24h = false;
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL_STRING("Rain from 9:45 PM", v.text);

    fixture_rain_now(&s_fix_ds);
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL_STRING("Rain now", v.text);
    TEST_ASSERT_EQUAL_STRING("1.2 mm/h", v.extra); /* 0.3 mm in the quarter hour */
    ctx.lang = lang_get("cs");
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL_STRING("Prší", v.text);
    TEST_ASSERT_EQUAL_STRING("1,2 mm/h", v.extra);

    fixture_rain_dry(&s_fix_ds);
    ctx.lang = lang_get("en");
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL_STRING("Dry for 2 h", v.text);
}

static void test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast(void)
{
    ui_context_t ctx = fixture_context();
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    ui_value_t v;
    ctx.now = (time_t)ds_weather(&s_fix_ds)->rain.t0 + 23 * 3600; /* four quarter hours left */
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    ctx.now = (time_t)ds_weather(&s_fix_ds)->rain.t0 + 22 * 3600; /* just the two hours */
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);

    ctx = fixture_context();
    fixture_forecast(&s_fix_ds, FIX_NOW - 3 * 3600);
    ds_set_forecast_ttl(&s_fix_ds, 2 * 3600); /* an interval sync of an hour that missed two */
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);

    ds_weather_t w = *ds_weather(&s_fix_ds);
    memset(w.rain.mm10, DS_RAIN_NONE, sizeof(w.rain.mm10)); /* Open-Meteo sent nulls: no data, not a dry spell */
    ds_set_weather(&s_fix_ds, &w);
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    w.rain.t0 = 0; /* a forecast from before M6 */
    ds_set_weather(&s_fix_ds, &w);
    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
}

static void test_the_rain_map_shows_a_frame_with_its_time(void)
{
    ui_value_t v = resolve(UI_FIELD_RAIN_MAP);
    TEST_ASSERT_EQUAL(UI_FK_RAIN_MAP, v.kind);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state); /* no radar at all */
    static radar_frame_t f;
    static ui_radar_t r;
    r = (ui_radar_t){ .wx_zoom_q = 26 };
    s_ctx.radar = &r;
    v = resolve(UI_FIELD_RAIN_MAP);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state); /* before the first frame */
    f.time = (uint32_t)(FIX_NOW - 8 * 60);
    r.frame = &f;
    v = resolve(UI_FIELD_RAIN_MAP);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("20:40", v.text);
    TEST_ASSERT_EQUAL_PTR(&r, v.radar);
    f.time = (uint32_t)(FIX_NOW - 3 * 3600);
    v = resolve(UI_FIELD_RAIN_MAP);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state); /* its age shows on the map, never as stale (spec §5.1) */
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
}

static void test_a_forecast_older_than_its_ttl_is_stale(void)
{
    fixture_forecast(&s_fix_ds, FIX_NOW - 30 * 3600);
    ui_value_t v = resolve(UI_FIELD_WX_TODAY);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(30 * 3600, v.age_s);
}

static void test_the_sun_rises_and_sets_over_brno(void)
{
    ui_value_t v = resolve(UI_FIELD_SUN_TIMES);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("06:44", v.text); /* 2026-09-25: Open-Meteo says 06:44 and 18:45 */
    TEST_ASSERT_EQUAL_STRING("18:45", v.extra);
    TEST_ASSERT_EQUAL_STRING("12 h 1 min, -4 min", v.detail); /* D26: Open-Meteo has 3.6 min less than the 24th */
}

/* The sun's next event, for a cell with room for one time (owner, 2026-10-05): the sunrise before it, the sunset in
 * the day, tomorrow's sunrise after the sunset. */
static void test_the_sun_names_its_next_event(void)
{
    s_ctx.local_day = FIX_DAY + 1; /* tomorrow's sunrise, to compare with */
    s_ctx.now = FIX_NOW + 86400;
    char tomorrow[16];
    snprintf(tomorrow, sizeof(tomorrow), "%s", resolve(UI_FIELD_SUN_TIMES).text);
    s_ctx = fixture_context(); /* 20:48, after the sunset */
    ui_value_t v = resolve(UI_FIELD_SUN_TIMES);
    TEST_ASSERT_FALSE(v.set_next);
    TEST_ASSERT_EQUAL_STRING(tomorrow, v.short_text);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(v.text, tomorrow)); /* not today's */
    s_ctx.now = FIX_NOW - 8 * 3600 - 48 * 60; /* 12:00 */
    s_ctx.local = fixture_local(12, 0, 0);
    v = resolve(UI_FIELD_SUN_TIMES);
    TEST_ASSERT_TRUE(v.set_next);
    TEST_ASSERT_EQUAL_STRING("18:45", v.short_text);
    s_ctx.now = FIX_NOW - 15 * 3600 - 48 * 60; /* 05:00 */
    s_ctx.local = fixture_local(5, 0, 0);
    v = resolve(UI_FIELD_SUN_TIMES);
    TEST_ASSERT_FALSE(v.set_next);
    TEST_ASSERT_EQUAL_STRING("06:44", v.short_text);
}

static void test_air_quality_and_pollen_name_their_band_and_level(void)
{
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    ui_value_t v = resolve(UI_FIELD_AQ_INDEX);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("38", v.text); /* 20:00's entry */
    TEST_ASSERT_EQUAL_STRING("Fair", v.extra);
    v = resolve(UI_FIELD_AQ_PM25);
    TEST_ASSERT_EQUAL_STRING("11", v.text);
    TEST_ASSERT_EQUAL_STRING("µg/m³", v.unit);
    v = resolve(UI_FIELD_POLLEN_TOP);
    TEST_ASSERT_EQUAL_STRING("Moderate", v.text); /* grass, 6 grains/m³ */
    TEST_ASSERT_EQUAL_STRING("Grass", v.extra);
    v = resolve(UI_FIELD_POLLEN_RAGWEED);
    TEST_ASSERT_EQUAL_STRING("Low", v.text);
    TEST_ASSERT_EQUAL_STRING("1/m³", v.extra);
    TEST_ASSERT_EQUAL_STRING("None", resolve(UI_FIELD_POLLEN_BIRCH).text);
}

static void test_the_uv_index_reads_rounded_in_the_who_bands(void)
{
    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    s_ctx.now = FIX_NOW - 7 * 3600 - 48 * 60; /* 13:00, the peak: 4.5 */
    ui_value_t v = resolve(UI_FIELD_AQ_UV);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("5", v.text);
    TEST_ASSERT_EQUAL_STRING("Moderate", v.extra);
    TEST_ASSERT_EQUAL_INT(5, v.bands);
    s_ctx.now = FIX_NOW; /* 20:48: night, 0 */
    TEST_ASSERT_EQUAL_STRING("Low", resolve(UI_FIELD_AQ_UV).extra);
}

static void test_old_readings_are_stale_with_their_age(void)
{
    s_ctx.now = FIX_NOW + 3 * 3600;
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
    TEST_ASSERT_EQUAL_STRING("23.4", v.text);
}

static void test_numbers_with_decimals_carry_a_whole_number_form(void)
{
    TEST_ASSERT_EQUAL_STRING("23", resolve(UI_FIELD_ENV_TEMP).short_text); /* 23.4 */
    ds_set(&s_fix_ds, DS_ENV_TEMP, -1250, FIX_NOW);
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL_STRING("-12.5", v.text);
    TEST_ASSERT_EQUAL_STRING("-13", v.short_text); /* half away from zero */
    s_ctx.fahrenheit = true;
    ds_set(&s_fix_ds, DS_ENV_TEMP, 3820, FIX_NOW);
    v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL_STRING("100.8", v.text);
    TEST_ASSERT_EQUAL_STRING("101", v.short_text);
    TEST_ASSERT_EQUAL_STRING("9", resolve(UI_FIELD_BAT_DAYS).short_text); /* 8.5 */
    TEST_ASSERT_EQUAL_STRING("", resolve(UI_FIELD_ENV_HUM).short_text);   /* no decimals to drop */
}

static void test_none_resolves_to_missing(void)
{
    ui_value_t v = resolve(UI_FIELD_NONE);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, v.field);
}

/* --- the PV forecast and the house (M6d) --- */

#define FIX_1320 (FIX_NOW - 7 * 3600 - 28 * 60) /* 13:20 CEST on the fixture's day */

static solar_forecast_t s_forecast;
static energy_reading_t s_reading;
static energy_day_t s_day;
static ui_solar_t s_solar;

/* A forecast fetched at 05:48 for the fixture's day and the next, and a reading at 13:17 with today's midnight
 * reading at 00:02; the context at 13:20. */
static void with_solar(void)
{
    memset(&s_forecast, 0, sizeof(s_forecast));
    s_forecast.day = FIX_DAY;
    s_forecast.fetched = (uint32_t)(FIX_NOW - 15 * 3600);
    s_forecast.q[0][51] = 360; /* 12:45: the day's peak, 3.6 kW */
    s_forecast.q[0][53] = 350; /* 13:15-13:30 */
    s_forecast.q[0][54] = 86;  /* 0.86 kW */
    s_forecast.q[1][48] = 100;
    s_forecast.wh[0] = 18400;
    s_forecast.wh[1] = 1400;
    s_forecast.wh[2] = 27400;
    energy_day_init(&s_day);
    s_day.day = FIX_DAY;
    s_day.base_at = (uint32_t)(FIX_1320 - 13 * 3600 - 18 * 60); /* 00:02 */
    s_day.base_to_wh = 1000000;
    s_day.base_from_wh = 2000000;
    s_reading = (energy_reading_t){ .at = (uint32_t)(FIX_1320 - 180), .pv_w = 3420, .grid_w = -2560, .load_w = 860,
                                    .bat_w = 0, .soc = -1, .inverter = 4, .yield_wh = 9400,
                                    .to_grid_wh = 1004900, .from_grid_wh = 2000600 };
    s_solar = (ui_solar_t){ .forecast = &s_forecast, .forecast_ttl_s = 26 * 3600, .reading = &s_reading,
                            .day = &s_day, .battery = false };
    s_ctx.solar = &s_solar;
    s_ctx.now = FIX_1320;
    s_ctx.local = fixture_local(13, 20, 0);
}

static void test_the_forecast_fields_read_the_quarter_hour_now(void)
{
    with_solar();
    ui_value_t v = resolve(UI_FIELD_PV_NOW);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("3.50", v.text); /* 13:15-13:30 */
    TEST_ASSERT_EQUAL_STRING("3.5", v.short_text);
    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
    TEST_ASSERT_EQUAL_STRING("Forecast now", v.label);
    v = resolve(UI_FIELD_PV_TODAY);
    TEST_ASSERT_EQUAL_STRING("18.4", v.text);
    TEST_ASSERT_EQUAL_STRING("18", v.short_text);
    TEST_ASSERT_EQUAL_STRING("kWh", v.unit);
    v = resolve(UI_FIELD_PV_LEFT); /* two thirds of 13:15's and all of 13:30's: 583 + 215 Wh */
    TEST_ASSERT_EQUAL_STRING("0.8", v.text);
    TEST_ASSERT_EQUAL_STRING("", v.short_text); /* small values keep their decimals */
    v = resolve(UI_FIELD_PV_TOMORROW);
    TEST_ASSERT_EQUAL_STRING("1.4", v.text);
    v = resolve(UI_FIELD_PV_PEAK);
    TEST_ASSERT_EQUAL_STRING("3.60", v.text);
    TEST_ASSERT_EQUAL_STRING("12:45", v.extra);
    s_ctx.clock_24h = false;
    v = resolve(UI_FIELD_PV_PEAK);
    TEST_ASSERT_EQUAL_STRING("12:45 PM", v.extra);
}

static void test_kilowatts_shorten_only_from_one_and_kilowatt_hours_from_ten(void)
{
    with_solar();
    s_forecast.q[0][53] = 86;
    ui_value_t v = resolve(UI_FIELD_PV_NOW);
    TEST_ASSERT_EQUAL_STRING("0.86", v.text);
    TEST_ASSERT_EQUAL_STRING("", v.short_text);
    s_forecast.q[0][53] = 1234; /* 12.34 kW: one decimal from 10 kW */
    v = resolve(UI_FIELD_PV_NOW);
    TEST_ASSERT_EQUAL_STRING("12.3", v.text);
    TEST_ASSERT_EQUAL_STRING("12", v.short_text);
    s_forecast.q[0][53] = 19999; /* 199.99 kW */
    v = resolve(UI_FIELD_PV_NOW);
    TEST_ASSERT_EQUAL_STRING("200.0", v.text);
    TEST_ASSERT_EQUAL_STRING("200", v.short_text);
    s_forecast.wh[0] = 9949;
    v = resolve(UI_FIELD_PV_TODAY);
    TEST_ASSERT_EQUAL_STRING("9.9", v.text);
    TEST_ASSERT_EQUAL_STRING("", v.short_text);
    s_ctx.lang = lang_get("cs");
    v = resolve(UI_FIELD_PV_TODAY);
    TEST_ASSERT_EQUAL_STRING("9,9", v.text);
    TEST_ASSERT_EQUAL_STRING("Předpověď dnes", v.label);
}

static void test_after_midnight_tomorrow_is_today(void)
{
    with_solar();
    s_ctx.now += 86400;
    s_ctx.local_day = FIX_DAY + 1;
    TEST_ASSERT_EQUAL_STRING("1.4", resolve(UI_FIELD_PV_TODAY).text);
    TEST_ASSERT_EQUAL_STRING("27.4", resolve(UI_FIELD_PV_TOMORROW).text); /* the third day's total */
    TEST_ASSERT_EQUAL_STRING("0.00", resolve(UI_FIELD_PV_NOW).text);
    s_ctx.now += 86400;
    s_ctx.local_day = FIX_DAY + 2;
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_NOW).state); /* no quarter hours for that day */
    TEST_ASSERT_EQUAL_STRING("27.4", resolve(UI_FIELD_PV_TODAY).text);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_TOMORROW).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_LEFT).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_PEAK).state);
}

static void test_the_forecast_goes_stale_after_its_wait(void)
{
    with_solar();
    s_ctx.now = (time_t)s_forecast.fetched + 26 * 3600 + 60;
    ui_value_t v = resolve(UI_FIELD_PV_TODAY);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(26 * 3600 + 60, v.age_s);
    s_solar.forecast_ttl_s = 0; /* sync mode manual: never stale, only old */
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, resolve(UI_FIELD_PV_TODAY).state);
}

static void test_without_a_forecast_or_a_reading_the_fields_are_missing(void)
{
    with_solar();
    s_forecast.day = 0;
    s_reading.at = 0;
    for (int f = UI_FIELD_PV_NOW; f <= UI_FIELD_EN_SELF; f++) {
        TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve((ui_field_id_t)f).state);
    }
    s_ctx.solar = NULL;
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_NOW).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_PV).state);
    TEST_ASSERT_EQUAL_STRING("Solar", resolve(UI_FIELD_EN_PV).label);
}

static void test_the_house_now(void)
{
    with_solar();
    ui_value_t v = resolve(UI_FIELD_EN_PV);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("3.42", v.text);
    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
    TEST_ASSERT_EQUAL_STRING("0.86", resolve(UI_FIELD_EN_LOAD).text);
    v = resolve(UI_FIELD_EN_GRID); /* exporting: its label in M and up, an arrow up in S and XS */
    TEST_ASSERT_EQUAL_STRING("2.56", v.text);
    TEST_ASSERT_EQUAL_STRING("Export", v.label);
    TEST_ASSERT_EQUAL_INT(1, v.trend);
    s_reading.grid_w = 430;
    v = resolve(UI_FIELD_EN_GRID);
    TEST_ASSERT_EQUAL_STRING("0.43", v.text);
    TEST_ASSERT_EQUAL_STRING("Import", v.label);
    TEST_ASSERT_EQUAL_INT(-1, v.trend);
    s_reading.grid_w = -12; /* under 20 W flows nowhere */
    v = resolve(UI_FIELD_EN_GRID);
    TEST_ASSERT_EQUAL_STRING("Grid", v.label);
    TEST_ASSERT_EQUAL_INT(0, v.trend);
}

static void test_the_home_battery(void)
{
    with_solar();
    s_reading.soc = 64;
    s_reading.bat_w = 1200;
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_BATTERY).state); /* it doesn't show */
    s_solar.battery = true;
    ui_value_t v = resolve(UI_FIELD_EN_BATTERY);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("64", v.text);
    TEST_ASSERT_EQUAL_STRING("%", v.unit);
    TEST_ASSERT_EQUAL_INT(64, v.percent);
    TEST_ASSERT_EQUAL(DS_BAT_CHARGING, v.battery); /* the bolt */
    TEST_ASSERT_EQUAL_INT(0, v.trend);
    TEST_ASSERT_EQUAL_STRING("1.20 kW", v.extra); /* its power in M and up */
    TEST_ASSERT_EQUAL_STRING("Home battery", v.label);
    s_reading.bat_w = -800;
    v = resolve(UI_FIELD_EN_BATTERY);
    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, v.battery);
    TEST_ASSERT_EQUAL_INT(-1, v.trend); /* the arrow down */
    TEST_ASSERT_EQUAL_STRING("0.80 kW", v.extra);
    s_reading.bat_w = 5;
    TEST_ASSERT_EQUAL_STRING("", resolve(UI_FIELD_EN_BATTERY).extra);
    s_reading.soc = -1; /* shown, but the reply has no charge */
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_BATTERY).state);
}

static void test_todays_totals(void)
{
    with_solar();
    ui_value_t v = resolve(UI_FIELD_EN_YIELD);
    TEST_ASSERT_EQUAL_STRING("9.4", v.text);
    TEST_ASSERT_EQUAL_STRING("kWh", v.unit);
    v = resolve(UI_FIELD_EN_EXPORT);
    TEST_ASSERT_EQUAL_STRING("4.9", v.text);
    TEST_ASSERT_EQUAL_INT(1, v.trend);
    v = resolve(UI_FIELD_EN_IMPORT);
    TEST_ASSERT_EQUAL_STRING("0.6", v.text);
    TEST_ASSERT_EQUAL_INT(-1, v.trend);
    v = resolve(UI_FIELD_EN_SELF);
    TEST_ASSERT_EQUAL_STRING("48", v.text); /* (9.4 - 4.9) / 9.4 */
    TEST_ASSERT_EQUAL_STRING("%", v.unit);
    s_day.base_at = 0; /* no reading near midnight */
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_EXPORT).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_IMPORT).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_SELF).state);
    TEST_ASSERT_EQUAL_STRING("9.4", resolve(UI_FIELD_EN_YIELD).text);
    s_ctx.now += 86400; /* yesterday's reading says nothing about today's totals */
    s_ctx.local_day = FIX_DAY + 1;
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_YIELD).state);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, resolve(UI_FIELD_EN_PV).state); /* the power is stale, not gone */
}

static void test_a_reading_goes_stale_after_15_minutes(void)
{
    with_solar();
    s_ctx.now = (time_t)s_reading.at + 15 * 60;
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, resolve(UI_FIELD_EN_PV).state);
    s_ctx.now += 60;
    ui_value_t v = resolve(UI_FIELD_EN_PV);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(16 * 60, v.age_s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names_map_both_ways);
    RUN_TEST(test_temperature_rounds_to_tenths_with_unit_and_trend);
    RUN_TEST(test_fahrenheit_converts_values);
    RUN_TEST(test_humidity_battery_and_days_left);
    RUN_TEST(test_clock_fields_follow_the_time_and_language);
    RUN_TEST(test_invalid_time_shows_dashes_and_hides_the_date);
    RUN_TEST(test_english_has_no_name_days_or_holidays_and_weather_waits_for_a_sync);
    RUN_TEST(test_the_weather_now_and_today_come_from_the_forecast);
    RUN_TEST(test_the_hourly_strip_starts_at_the_next_hour_every_two_hours);
    RUN_TEST(test_rain_in_the_next_two_hours_says_when);
    RUN_TEST(test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast);
    RUN_TEST(test_the_rain_map_shows_a_frame_with_its_time);
    RUN_TEST(test_a_forecast_older_than_its_ttl_is_stale);
    RUN_TEST(test_the_sun_rises_and_sets_over_brno);
    RUN_TEST(test_the_sun_names_its_next_event);
    RUN_TEST(test_air_quality_and_pollen_name_their_band_and_level);
    RUN_TEST(test_the_uv_index_reads_rounded_in_the_who_bands);
    RUN_TEST(test_old_readings_are_stale_with_their_age);
    RUN_TEST(test_none_resolves_to_missing);
    RUN_TEST(test_numbers_with_decimals_carry_a_whole_number_form);
    RUN_TEST(test_the_forecast_fields_read_the_quarter_hour_now);
    RUN_TEST(test_kilowatts_shorten_only_from_one_and_kilowatt_hours_from_ten);
    RUN_TEST(test_after_midnight_tomorrow_is_today);
    RUN_TEST(test_the_forecast_goes_stale_after_its_wait);
    RUN_TEST(test_without_a_forecast_or_a_reading_the_fields_are_missing);
    RUN_TEST(test_the_house_now);
    RUN_TEST(test_the_home_battery);
    RUN_TEST(test_todays_totals);
    RUN_TEST(test_a_reading_goes_stale_after_15_minutes);
    return UNITY_END();
}
