#include "battery_model.h"
#include "unity.h"

static battery_gauge_t s_g;

void setUp(void)
{
    battery_gauge_init(&s_g);
}

void tearDown(void) {}

/* Adds one sample every 5 min from t0, all at the same voltage. */
static uint32_t add_steady(uint32_t t0, int mv, int count)
{
    for (int i = 0; i < count; i++) {
        battery_gauge_add(&s_g, t0 + (uint32_t)i * 300u, mv);
    }
    return t0 + (uint32_t)(count - 1) * 300u;
}

static void test_ocv_curve_interpolates_and_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4250));
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4200));
    TEST_ASSERT_EQUAL_INT(50, battery_percent_from_mv(3840));
    TEST_ASSERT_EQUAL_INT(48, battery_percent_from_mv(3830));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3270));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3000));
}

static void test_no_sample_means_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_level(&s_g));
    TEST_ASSERT_EQUAL_INT(0, battery_gauge_mv(&s_g));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_gauge_state(&s_g, 1000));
}

static void test_first_sample_sets_voltage_and_level(void)
{
    battery_gauge_add(&s_g, 1000, 3840);
    TEST_ASSERT_EQUAL_INT(3840, battery_gauge_mv(&s_g));
    TEST_ASSERT_EQUAL_INT(50, battery_gauge_level(&s_g));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_gauge_state(&s_g, 1000));
}

static void test_samples_are_smoothed(void)
{
    battery_gauge_add(&s_g, 1000, 4000);
    battery_gauge_add(&s_g, 1300, 3900);
    TEST_ASSERT_EQUAL_INT(3975, battery_gauge_mv(&s_g));
}

static void test_level_does_not_rise_while_not_charging(void)
{
    battery_gauge_add(&s_g, 1000, 3800);
    TEST_ASSERT_EQUAL_INT(40, battery_gauge_level(&s_g));
    battery_gauge_add(&s_g, 1300, 3900); /* a recovery bump after load, not a charge */
    TEST_ASSERT_EQUAL_INT(40, battery_gauge_level(&s_g));
}

static void test_steady_voltage_after_30_min_is_discharging(void)
{
    uint32_t t = add_steady(1000, 3840, 7);
    TEST_ASSERT_EQUAL(BATTERY_DISCHARGING, battery_gauge_state(&s_g, t));
}

static void test_rising_voltage_over_30_min_is_charging_and_raises_the_level(void)
{
    uint32_t t = 1000;
    for (int i = 0; i < 8; i++, t += 300) {
        battery_gauge_add(&s_g, t, 3800 + i * 10); /* +70 mV over 35 min */
    }
    TEST_ASSERT_EQUAL(BATTERY_CHARGING, battery_gauge_state(&s_g, t - 300));
    TEST_ASSERT_TRUE(battery_gauge_level(&s_g) > 40);
}

static void test_high_steady_voltage_is_full(void)
{
    uint32_t t = add_steady(1000, 4180, 8);
    TEST_ASSERT_EQUAL(BATTERY_FULL, battery_gauge_state(&s_g, t));
    TEST_ASSERT_EQUAL_INT(98, battery_gauge_level(&s_g));
}

static void test_extra_samples_do_not_crowd_the_history(void)
{
    battery_gauge_add(&s_g, 1000, 3840);
    for (int i = 1; i <= 20; i++) {
        battery_gauge_add(&s_g, 1000 + (uint32_t)i * 10u, 3840); /* e.g. BOOT refreshes */
    }
    TEST_ASSERT_EQUAL_UINT8(1, s_g.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ocv_curve_interpolates_and_clamps);
    RUN_TEST(test_no_sample_means_unknown);
    RUN_TEST(test_first_sample_sets_voltage_and_level);
    RUN_TEST(test_samples_are_smoothed);
    RUN_TEST(test_level_does_not_rise_while_not_charging);
    RUN_TEST(test_steady_voltage_after_30_min_is_discharging);
    RUN_TEST(test_rising_voltage_over_30_min_is_charging_and_raises_the_level);
    RUN_TEST(test_high_steady_voltage_is_full);
    RUN_TEST(test_extra_samples_do_not_crowd_the_history);
    return UNITY_END();
}
