#include <string.h>

#include "datastore.h"
#include "unity.h"

/* The weather and air quality in the datastore (spec §5.1, §6). */

static ds_t s_ds;
#define HOUR0 1790805600u /* 2026-10-01 00:00 CEST: the first hourly entry, a local midnight */
#define LOCAL_DAY0 20727  /* 2026-10-01 in days since 1970-01-01 */

void setUp(void)
{
    ds_init(&s_ds);
}

void tearDown(void) {}

/* A forecast fetched at 06:00 local, with its current block from then: 12.3 °C, mainly clear. */
static ds_weather_t forecast(void)
{
    ds_weather_t w;
    memset(&w, 0, sizeof(w));
    w.fetched = HOUR0 + 6 * 3600;
    w.now_time = HOUR0 + 6 * 3600;
    w.now_temp_c10 = 123;
    w.now_feels_c10 = 101;
    w.now_wind_kmh10 = 152;
    w.now_hum = 70;
    w.now_code = 1;
    w.now_is_day = 1;
    w.hour0 = HOUR0;
    for (int i = 0; i < DS_WX_HOURS; i++) {
        w.hours[i] = (ds_wx_hour_t){ .temp_c10 = (int16_t)(100 + i), .code = 3, .precip = (uint8_t)i };
    }
    w.day0_local = LOCAL_DAY0;
    for (int d = 0; d < DS_WX_DAYS; d++) {
        w.days[d] = (ds_wx_day_t){ .min_c10 = (int16_t)(90 + d), .max_c10 = (int16_t)(200 + d), .code = 61, .precip = 40 };
    }
    return w;
}

static void test_nothing_is_there_before_a_fetch(void)
{
    ds_wx_now_t now;
    TEST_ASSERT_NULL(ds_weather(&s_ds));
    TEST_ASSERT_NULL(ds_air(&s_ds));
    TEST_ASSERT_EQUAL(DS_MISSING, ds_weather_freshness(&s_ds, HOUR0));
    TEST_ASSERT_EQUAL(DS_MISSING, ds_air_freshness(&s_ds, HOUR0));
    TEST_ASSERT_FALSE(ds_weather_now(&s_ds, HOUR0, &now));
}

static void test_a_forecast_is_fresh_then_stale_after_its_ttl(void)
{
    ds_weather_t w = forecast();
    ds_set_forecast_ttl(&s_ds, 26 * 3600); /* a daily sync: 24 h plus 2 h (spec §5.1) */
    ds_set_weather(&s_ds, &w);
    TEST_ASSERT_TRUE(ds_take_changes(&s_ds) & DS_CHANGE_WEATHER);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_weather_freshness(&s_ds, w.fetched + 26 * 3600));
    TEST_ASSERT_EQUAL(DS_STALE, ds_weather_freshness(&s_ds, w.fetched + 26 * 3600 + 1));
}

static void test_a_manual_schedule_never_goes_stale(void)
{
    ds_weather_t w = forecast();
    ds_set_forecast_ttl(&s_ds, 0); /* spec §9.3: data only shows its age */
    ds_set_weather(&s_ds, &w);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_weather_freshness(&s_ds, HOUR0 + 60 * 3600));
}

static void test_a_forecast_is_missing_past_its_last_hour(void)
{
    ds_weather_t w = forecast();
    ds_set_forecast_ttl(&s_ds, 0);
    ds_set_weather(&s_ds, &w);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_weather_freshness(&s_ds, HOUR0 + DS_WX_HOURS * 3600 - 1));
    TEST_ASSERT_EQUAL(DS_MISSING, ds_weather_freshness(&s_ds, HOUR0 + DS_WX_HOURS * 3600));
}

static void test_now_is_the_current_block_for_an_hour_then_the_hourly_entry(void)
{
    ds_weather_t w = forecast();
    ds_set_weather(&s_ds, &w);
    ds_wx_now_t now;
    TEST_ASSERT_TRUE(ds_weather_now(&s_ds, w.now_time + 3600, &now));
    TEST_ASSERT_EQUAL_INT16(123, now.temp_c10);
    TEST_ASSERT_EQUAL_INT16(101, now.feels_c10);
    TEST_ASSERT_EQUAL_UINT8(1, now.code);
    TEST_ASSERT_EQUAL_INT8(1, now.is_day);
    TEST_ASSERT_EQUAL_UINT8(70, now.hum);
    TEST_ASSERT_EQUAL_UINT8(7, now.precip); /* the probability always comes from the hour: 07:00 */

    TEST_ASSERT_TRUE(ds_weather_now(&s_ds, w.now_time + 3601, &now)); /* 07:00:01: the 07:00 entry */
    TEST_ASSERT_EQUAL_INT16(107, now.temp_c10);
    TEST_ASSERT_EQUAL_UINT8(3, now.code);
    TEST_ASSERT_EQUAL_INT8(-1, now.is_day); /* the hourly entry doesn't say: the caller asks astro */
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, now.feels_c10);
    TEST_ASSERT_EQUAL_UINT8(DS_WX_NO_PCT, now.hum);
}

static void test_hour_index_follows_the_first_entry(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ds_hour_index(HOUR0, HOUR0 - 1));
    TEST_ASSERT_EQUAL_INT(0, ds_hour_index(HOUR0, HOUR0));
    TEST_ASSERT_EQUAL_INT(0, ds_hour_index(HOUR0, HOUR0 + 3599));
    TEST_ASSERT_EQUAL_INT(71, ds_hour_index(HOUR0, HOUR0 + 72 * 3600 - 1));
    TEST_ASSERT_EQUAL_INT(-1, ds_hour_index(HOUR0, HOUR0 + 72 * 3600));
    TEST_ASSERT_EQUAL_INT(-1, ds_hour_index(0, HOUR0)); /* never fetched */
}

static void test_air_quality_now_is_this_hours_entry(void)
{
    ds_air_t a;
    memset(&a, 0, sizeof(a));
    a.fetched = HOUR0 + 6 * 3600;
    a.hour0 = HOUR0;
    for (int i = 0; i < DS_WX_HOURS; i++) {
        a.aqi[i] = (uint8_t)(20 + i);
        a.pm25[i] = (uint8_t)i;
        a.pm10[i] = (uint8_t)(2 * i);
    }
    a.day0_local = LOCAL_DAY0;
    memset(a.pollen, 0xFF, sizeof(a.pollen));
    a.pollen[0][DS_POLLEN_GRASS] = 60; /* 6.0 grains/m³ */
    ds_set_forecast_ttl(&s_ds, 26 * 3600);
    ds_set_air(&s_ds, &a);
    TEST_ASSERT_TRUE(ds_take_changes(&s_ds) & DS_CHANGE_AIR);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_air_freshness(&s_ds, HOUR0 + 13 * 3600));
    const ds_air_t *got = ds_air(&s_ds);
    TEST_ASSERT_NOT_NULL(got);
    int h = ds_hour_index(got->hour0, HOUR0 + 13 * 3600 + 5);
    TEST_ASSERT_EQUAL_INT(13, h);
    TEST_ASSERT_EQUAL_UINT8(33, got->aqi[h]);
    TEST_ASSERT_EQUAL_UINT16(60, got->pollen[0][DS_POLLEN_GRASS]);
    TEST_ASSERT_EQUAL_UINT16(DS_POLLEN_NONE, got->pollen[0][DS_POLLEN_BIRCH]);
}

static void test_a_new_fetch_replaces_the_old_one_whole(void)
{
    ds_weather_t w = forecast();
    ds_set_weather(&s_ds, &w);
    w.now_temp_c10 = -45;
    w.fetched += 3600;
    w.now_time += 3600;
    ds_set_weather(&s_ds, &w);
    TEST_ASSERT_EQUAL_INT16(-45, ds_weather(&s_ds)->now_temp_c10);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_there_before_a_fetch);
    RUN_TEST(test_a_forecast_is_fresh_then_stale_after_its_ttl);
    RUN_TEST(test_a_manual_schedule_never_goes_stale);
    RUN_TEST(test_a_forecast_is_missing_past_its_last_hour);
    RUN_TEST(test_now_is_the_current_block_for_an_hour_then_the_hourly_entry);
    RUN_TEST(test_hour_index_follows_the_first_entry);
    RUN_TEST(test_air_quality_now_is_this_hours_entry);
    RUN_TEST(test_a_new_fetch_replaces_the_old_one_whole);
    return UNITY_END();
}
