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

static void test_english_has_no_name_days_or_holidays_and_weather_waits_for_m5(void)
{
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_NAMEDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_HOLIDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_WX_NOW).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_SUN_TIMES).state);
}

static void test_old_readings_are_stale_with_their_age(void)
{
    s_ctx.now = FIX_NOW + 3 * 3600;
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
    TEST_ASSERT_EQUAL_STRING("23.4", v.text);
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
    RUN_TEST(test_english_has_no_name_days_or_holidays_and_weather_waits_for_m5);
    RUN_TEST(test_old_readings_are_stale_with_their_age);
    RUN_TEST(test_none_resolves_to_missing);
    return UNITY_END();
}
