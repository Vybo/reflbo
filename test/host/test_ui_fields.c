#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "context_fixtures.h"
#include "ui_fields.h"
#include "ui_radar.h"
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

/* mqtt.<key> (spec §12.5): the mapping's label and unit, the value in the device's language. */
static void test_mqtt_fields_come_from_the_store(void)
{
    fixture_mqtt(&s_ctx);
    ui_value_t v = resolve(FIX_MQTT(0));
    TEST_ASSERT_EQUAL(FIX_MQTT(0), v.field);
    TEST_ASSERT_EQUAL(UI_FK_NUMBER, v.kind);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("Outside", v.label);
    TEST_ASSERT_EQUAL_STRING("21.5", v.text);
    TEST_ASSERT_EQUAL_STRING("22", v.short_text);
    TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", v.unit);
    v = resolve(FIX_MQTT(1));
    TEST_ASSERT_EQUAL_STRING("612", v.text);
    TEST_ASSERT_EQUAL_STRING("", v.short_text); /* no decimals to drop */
    TEST_ASSERT_EQUAL_STRING("ppm", v.unit);
    v = resolve(FIX_MQTT(2));
    TEST_ASSERT_EQUAL(UI_FK_TEXT, v.kind);
    TEST_ASSERT_EQUAL_STRING("Front door", v.label);
    TEST_ASSERT_EQUAL_STRING("Closed", v.text);
    TEST_ASSERT_EQUAL_STRING("", v.unit);
    s_ctx.lang = lang_get("cs");
    TEST_ASSERT_EQUAL_STRING("21,5", resolve(FIX_MQTT(0)).text);
    s_ctx.lang = lang_get("en");
    /* the largest values a payload brings: the whole number rounds without overflow, where long is 32 bits */
    s_fix_mqtt.entry[0].number = INT32_MAX;
    TEST_ASSERT_EQUAL_STRING("214748364.7", resolve(FIX_MQTT(0)).text);
    TEST_ASSERT_EQUAL_STRING("214748365", resolve(FIX_MQTT(0)).short_text);
    s_fix_mqtt.entry[0].number = -INT32_MAX;
    TEST_ASSERT_EQUAL_STRING("-214748365", resolve(FIX_MQTT(0)).short_text);
}

static void test_mqtt_values_go_stale_with_their_age(void)
{
    fixture_mqtt(&s_ctx);
    ui_value_t v = resolve(FIX_MQTT(3));
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
    TEST_ASSERT_EQUAL_STRING("1.24", v.text);
    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
}

/* A mapped key without a value yet is missing; a key no mapping names is an empty slot (spec §12.5). */
static void test_unmapped_keys_are_empty_slots(void)
{
    fixture_mqtt(&s_ctx);
    ui_value_t v = resolve(FIX_MQTT(4));
    TEST_ASSERT_EQUAL(FIX_MQTT(4), v.field);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL(UI_FK_TEXT, v.kind);
    TEST_ASSERT_EQUAL_STRING("Washer", v.label);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(5)).field); /* window */
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(6)).field); /* no such key */
    s_ctx.mqtt = NULL;
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(0)).field);
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
    RUN_TEST(test_rain_in_the_next_two_hours_says_when);
    RUN_TEST(test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast);
    RUN_TEST(test_the_rain_map_shows_a_frame_with_its_time);
    RUN_TEST(test_a_forecast_older_than_its_ttl_is_stale);
    RUN_TEST(test_the_sun_rises_and_sets_over_brno);
    RUN_TEST(test_air_quality_and_pollen_name_their_band_and_level);
    RUN_TEST(test_the_uv_index_reads_rounded_in_the_who_bands);
    RUN_TEST(test_old_readings_are_stale_with_their_age);
    RUN_TEST(test_none_resolves_to_missing);
    RUN_TEST(test_numbers_with_decimals_carry_a_whole_number_form);
    RUN_TEST(test_mqtt_fields_come_from_the_store);
    RUN_TEST(test_mqtt_values_go_stale_with_their_age);
    RUN_TEST(test_unmapped_keys_are_empty_slots);
    return UNITY_END();
}
