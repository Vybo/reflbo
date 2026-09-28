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

/* The review found that a 240 s history spacing with 8 slots spans only 28 min, so charging was
 * never seen at 4-minute samples and flickered at 1 and 2 minutes. */
static void test_charging_is_seen_at_every_sample_interval(void)
{
    const uint32_t intervals[] = { 60, 120, 180, 240, 300, 600 };
    for (unsigned i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        battery_gauge_init(&s_g);
        int missed = 0;
        for (uint32_t elapsed = 0; elapsed <= 3 * 3600; elapsed += intervals[i]) {
            uint32_t now = 1000 + elapsed;
            battery_gauge_add(&s_g, now, 3700 + (int)(elapsed * 2 / 60)); /* +2 mV per minute */
            if (elapsed >= 40 * 60 && battery_gauge_state(&s_g, now) != BATTERY_CHARGING) {
                missed++;
            }
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, missed, "a sample interval hid the charging trend");
    }
}

static void test_percent_in_tenths_matches_the_curve(void)
{
    TEST_ASSERT_EQUAL_INT(500, battery_percent10_from_mv(3840));
    TEST_ASSERT_EQUAL_INT(475, battery_percent10_from_mv(3830));
    TEST_ASSERT_EQUAL_INT(1000, battery_percent10_from_mv(4300));
    TEST_ASSERT_EQUAL_INT(0, battery_percent10_from_mv(3000));
}

/* A 5-min sample every step, falling `mv_per_hour` steadily from `mv0`. */
static uint32_t discharge(uint32_t t0, int mv0, int mv_per_hour, int hours)
{
    uint32_t t = t0;
    for (int i = 0; i <= hours * 12; i++) {
        t = t0 + (uint32_t)i * 300u;
        battery_gauge_add(&s_g, t, mv0 - mv_per_hour * i / 12);
    }
    return t;
}

static void test_days_left_after_six_hours_of_discharge(void)
{
    /* 3.79 V down 2 mV an hour, where the curve is 2.5 % per 10 mV throughout: 0.5 % an hour. After
     * 8 h the level is 31 %, so 31 / 0.5 / 24 = 2.6 days are left. */
    uint32_t t = discharge(1000, 3790, 2, 8);
    TEST_ASSERT_INT_WITHIN(3, 26, battery_gauge_days_left10(&s_g, t));
}

static void test_no_days_left_before_six_hours_or_without_a_drop(void)
{
    uint32_t t = discharge(1000, 3900, 10, 5);
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
    battery_gauge_init(&s_g);
    t = add_steady(1000, 3800, 12 * 8); /* flat for 8 h: no measurable drop */
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
}

static void test_charging_restarts_the_days_left_history(void)
{
    uint32_t t = discharge(1000, 3790, 2, 8);
    for (int i = 1; i <= 12; i++) { /* an hour on the charger: +100 mV */
        battery_gauge_add(&s_g, t + (uint32_t)i * 300u, 3774 + 100 * i / 12);
    }
    t += 3600;
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
    TEST_ASSERT_EQUAL_INT(0, s_g.level_count);
}

/* The clock moves when it is set (a board without the RTC cell starts in 2000, spec §7). */
#define YEAR_2000 946684800u

static void test_a_shifted_history_keeps_the_estimate_across_a_clock_set(void)
{
    uint32_t t = discharge(YEAR_2000, 3790, 2, 8);
    int64_t delta = 26LL * 365 * 86400;
    battery_gauge_shift_time(&s_g, delta); /* what the app does when the time is set */
    uint32_t now = t + (uint32_t)delta + 300;
    battery_gauge_add(&s_g, now, 3774);
    TEST_ASSERT_INT_WITHIN(3, 26, battery_gauge_days_left10(&s_g, now));
    TEST_ASSERT_EQUAL(BATTERY_DISCHARGING, battery_gauge_state(&s_g, now));
}

static void test_a_clock_moved_forward_gives_no_estimate_rather_than_a_wrong_one(void)
{
    uint32_t t = discharge(YEAR_2000, 3790, 2, 8);
    uint32_t later = t + 26u * 365 * 86400; /* set to the real date, nobody shifted the history */
    for (int i = 1; i <= 12; i++) {
        battery_gauge_add(&s_g, later + (uint32_t)i * 300u, 3774);
    }
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, later + 3600));
}

static void test_a_clock_moved_back_restarts_the_history(void)
{
    uint32_t t = discharge(1000000, 3790, 2, 8);
    battery_gauge_add(&s_g, t - 3 * 3600, 3774); /* the clock was a few hours ahead */
    TEST_ASSERT_EQUAL_INT(1, s_g.level_count);
    TEST_ASSERT_EQUAL_INT(1, s_g.count);
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t - 3 * 3600));
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
    RUN_TEST(test_charging_is_seen_at_every_sample_interval);
    RUN_TEST(test_percent_in_tenths_matches_the_curve);
    RUN_TEST(test_days_left_after_six_hours_of_discharge);
    RUN_TEST(test_no_days_left_before_six_hours_or_without_a_drop);
    RUN_TEST(test_charging_restarts_the_days_left_history);
    RUN_TEST(test_a_shifted_history_keeps_the_estimate_across_a_clock_set);
    RUN_TEST(test_a_clock_moved_forward_gives_no_estimate_rather_than_a_wrong_one);
    RUN_TEST(test_a_clock_moved_back_restarts_the_history);
    return UNITY_END();
}
