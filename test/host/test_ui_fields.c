#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "context_fixtures.h"
#include "ui_fields.h"
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
    RUN_TEST(test_a_forecast_older_than_its_ttl_is_stale);
    RUN_TEST(test_the_sun_rises_and_sets_over_brno);
    RUN_TEST(test_air_quality_and_pollen_name_their_band_and_level);
    RUN_TEST(test_the_uv_index_reads_rounded_in_the_who_bands);
    RUN_TEST(test_old_readings_are_stale_with_their_age);
    RUN_TEST(test_none_resolves_to_missing);
    RUN_TEST(test_numbers_with_decimals_carry_a_whole_number_form);
    return UNITY_END();
}
