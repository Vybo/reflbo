#include "datastore.h"
#include "unity.h"

static ds_t s_ds;
#define T0  1790000000 /* a UTC time in 2026 */
#define DAY 20720

void setUp(void)
{
    ds_init(&s_ds);
}

void tearDown(void) {}

static ds_entry_t get(ds_field_t f)
{
    ds_entry_t e = { 0 };
    TEST_ASSERT_TRUE(ds_get(&s_ds, f, &e));
    return e;
}

static void test_fields_start_missing(void)
{
    ds_entry_t e;
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        TEST_ASSERT_FALSE(ds_get(&s_ds, (ds_field_t)f, &e));
        TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, (ds_field_t)f, T0, DAY));
    }
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
}

static void test_env_reading_sets_temperature_humidity_and_dew_point(void)
{
    ds_set_env(&s_ds, 2500, 5000, T0, DAY);
    TEST_ASSERT_EQUAL_INT32(2500, get(DS_ENV_TEMP).value);
    TEST_ASSERT_EQUAL_INT32(5000, get(DS_ENV_HUM).value);
    TEST_ASSERT_INT32_WITHIN(10, 1386, get(DS_ENV_DEW).value); /* 25 °C at 50 % → 13.86 °C */
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0, DAY));
    uint32_t changes = ds_take_changes(&s_ds);
    TEST_ASSERT_TRUE(changes & (1u << DS_ENV_TEMP));
    TEST_ASSERT_TRUE(changes & (1u << DS_ENV_DEW));
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
}

static void test_dew_point_below_zero_and_at_saturation(void)
{
    ds_set_env(&s_ds, -500, 8000, T0, DAY);
    TEST_ASSERT_INT32_WITHIN(5, -792, get(DS_ENV_DEW).value); /* -5 °C at 80 % → -7.92 °C */
    ds_set_env(&s_ds, 1000, 10000, T0 + 300, DAY);
    TEST_ASSERT_INT32_WITHIN(1, 1000, get(DS_ENV_DEW).value);
}

static void test_no_dew_point_below_one_percent_humidity(void)
{
    ds_set_env(&s_ds, 2500, 50, T0, DAY);
    ds_entry_t e;
    TEST_ASSERT_FALSE(ds_get(&s_ds, DS_ENV_DEW, &e));
}

static void test_values_go_stale_after_their_ttl(void)
{
    ds_set_env(&s_ds, 2100, 4000, T0, DAY);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 900, DAY));
    TEST_ASSERT_EQUAL(DS_STALE, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 901, DAY));
    ds_set_ttl(&s_ds, DS_ENV_TEMP, 3600);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 901, DAY));
}

static void test_min_and_max_follow_the_day_and_restart_at_midnight(void)
{
    ds_set_env(&s_ds, 2100, 4000, T0, DAY);
    ds_set_env(&s_ds, 1900, 4000, T0 + 600, DAY);
    ds_set_env(&s_ds, 2300, 4000, T0 + 1200, DAY);
    TEST_ASSERT_EQUAL_INT32(1900, get(DS_ENV_TEMP_MIN).value);
    TEST_ASSERT_EQUAL_INT32(2300, get(DS_ENV_TEMP_MAX).value);
    TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, DS_ENV_TEMP_MAX, T0 + 1300, DAY + 1));
    ds_set_env(&s_ds, 2200, 4000, T0 + 1800, DAY + 1);
    TEST_ASSERT_EQUAL_INT32(2200, get(DS_ENV_TEMP_MIN).value);
    TEST_ASSERT_EQUAL_INT32(2200, get(DS_ENV_TEMP_MAX).value);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP_MIN, T0 + 1800, DAY + 1));
}

static void test_trend_compares_with_the_reading_an_hour_ago(void)
{
    for (int i = 0; i <= 12; i++) { /* every 5 min for an hour, rising 0.1 °C and 0.5 % each */
        ds_set_env(&s_ds, 2000 + 10 * i, 4000 + 50 * i, T0 + 300 * i, DAY);
    }
    TEST_ASSERT_EQUAL_INT32(120, get(DS_ENV_TEMP).trend);
    TEST_ASSERT_EQUAL_INT32(600, get(DS_ENV_HUM).trend);
}

static void test_no_trend_without_an_hour_of_history(void)
{
    ds_set_env(&s_ds, 2000, 4000, T0, DAY);
    ds_set_env(&s_ds, 2100, 4000, T0 + 3000, DAY); /* 50 min */
    TEST_ASSERT_EQUAL_INT32(DS_NO_TREND, get(DS_ENV_TEMP).trend);
    ds_set_env(&s_ds, 2200, 4000, T0 + 9000, DAY); /* only readings older than 90 min: too old */
    TEST_ASSERT_EQUAL_INT32(DS_NO_TREND, get(DS_ENV_TEMP).trend);
}

static void test_frequent_readings_do_not_crowd_the_history(void)
{
    for (int i = 0; i <= 60; i++) { /* every minute for an hour */
        ds_set_env(&s_ds, 2000 + i, 4000, T0 + 60 * i, DAY);
    }
    TEST_ASSERT_EQUAL_INT32(60, get(DS_ENV_TEMP).trend);
    TEST_ASSERT_TRUE(s_ds.env_count <= 13);
}

static void test_battery_carries_voltage_and_state(void)
{
    ds_set_battery(&s_ds, 83, 4051, DS_BAT_DISCHARGING, T0);
    ds_entry_t e = get(DS_BAT_LEVEL);
    TEST_ASSERT_EQUAL_INT32(83, e.value);
    TEST_ASSERT_EQUAL_INT(4051, e.mv);
    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, e.bat_state);
}

static void test_set_and_clear_any_field(void)
{
    ds_set(&s_ds, DS_BAT_DAYS, 92, T0);
    TEST_ASSERT_EQUAL_INT32(92, get(DS_BAT_DAYS).value);
    ds_take_changes(&s_ds);
    ds_clear(&s_ds, DS_BAT_DAYS);
    TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, DS_BAT_DAYS, T0, DAY));
    TEST_ASSERT_EQUAL_HEX32(1u << DS_BAT_DAYS, ds_take_changes(&s_ds));
    ds_clear(&s_ds, DS_BAT_DAYS); /* already clear: no change */
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
    ds_set(&s_ds, DS_FIELD_COUNT, 1, T0); /* out of range: ignored */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fields_start_missing);
    RUN_TEST(test_env_reading_sets_temperature_humidity_and_dew_point);
    RUN_TEST(test_dew_point_below_zero_and_at_saturation);
    RUN_TEST(test_no_dew_point_below_one_percent_humidity);
    RUN_TEST(test_values_go_stale_after_their_ttl);
    RUN_TEST(test_min_and_max_follow_the_day_and_restart_at_midnight);
    RUN_TEST(test_trend_compares_with_the_reading_an_hour_ago);
    RUN_TEST(test_no_trend_without_an_hour_of_history);
    RUN_TEST(test_frequent_readings_do_not_crowd_the_history);
    RUN_TEST(test_battery_carries_voltage_and_state);
    RUN_TEST(test_set_and_clear_any_field);
    return UNITY_END();
}
