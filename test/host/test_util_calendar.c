#include "unity.h"
#include "util_calendar.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

static time_t utc(int y, int mo, int d, int h, int mi, int s)
{
    return (time_t)(util_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s);
}

static void test_iso_weeks_including_year_boundaries(void)
{
    TEST_ASSERT_EQUAL_INT(39, util_iso_week(2026, 9, 25));
    TEST_ASSERT_EQUAL_INT(1, util_iso_week(2026, 1, 1));   /* a Thursday */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2027, 1, 1));  /* 2026 has 53 weeks */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2026, 12, 31));
    TEST_ASSERT_EQUAL_INT(52, util_iso_week(2023, 1, 1));  /* a Sunday, still in 2022's last week */
    TEST_ASSERT_EQUAL_INT(1, util_iso_week(2024, 12, 30)); /* a Monday, already week 1 of 2025 */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2021, 1, 3));  /* 2020 is a leap year starting on a Wednesday */
}

/* Reference instants from PyEphem (UTC). */
static void check_phase(time_t t, int index, int min_illum, int max_illum)
{
    util_moon_t m = util_moon_phase(t);
    TEST_ASSERT_EQUAL_INT(index, m.index);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(min_illum, m.illumination);
    TEST_ASSERT_LESS_OR_EQUAL_INT(max_illum, m.illumination);
}

static void test_new_quarter_and_full_moons_of_2026(void)
{
    check_phase(utc(2026, 1, 18, 19, 51, 55), 0, 0, 1);
    check_phase(utc(2026, 2, 17, 12, 1, 5), 0, 0, 1);
    check_phase(utc(2026, 1, 26, 4, 47, 21), 2, 47, 53);
    check_phase(utc(2026, 2, 24, 12, 27, 33), 2, 47, 53);
    check_phase(utc(2026, 1, 3, 10, 2, 51), 4, 99, 100);
    check_phase(utc(2026, 3, 3, 11, 37, 50), 4, 99, 100);
    check_phase(utc(2026, 1, 10, 15, 48, 21), 6, 47, 53);
    check_phase(utc(2026, 2, 9, 12, 43, 4), 6, 47, 53);
}

static void test_illumination_between_phases(void)
{
    TEST_ASSERT_INT_WITHIN(2, 96, util_moon_phase(utc(2026, 9, 28, 12, 0, 0)).illumination);
    TEST_ASSERT_INT_WITHIN(2, 99, util_moon_phase(utc(2026, 12, 25, 0, 0, 0)).illumination);
    TEST_ASSERT_INT_WITHIN(2, 18, util_moon_phase(utc(2027, 6, 1, 0, 0, 0)).illumination);
}

static void test_waxing_before_full_and_waning_after(void)
{
    util_moon_t before = util_moon_phase(utc(2026, 2, 28, 0, 0, 0));
    util_moon_t after = util_moon_phase(utc(2026, 3, 7, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT(3, before.index);
    TEST_ASSERT_EQUAL_INT(5, after.index);
    TEST_ASSERT_TRUE(before.age < 0.5 && after.age > 0.5);
}

static void check_easter(int year, int month, int day)
{
    int m = 0, d = 0;
    util_easter(year, &m, &d);
    TEST_ASSERT_EQUAL_INT_MESSAGE(month, m, "month");
    TEST_ASSERT_EQUAL_INT_MESSAGE(day, d, "day");
}

static void test_easter_sunday_including_the_earliest_and_latest_dates(void)
{
    check_easter(2024, 3, 31);
    check_easter(2025, 4, 20);
    check_easter(2026, 4, 5);
    check_easter(2027, 3, 28);
    check_easter(2008, 3, 23);
    check_easter(2038, 4, 25); /* the latest possible date */
    check_easter(2285, 3, 22); /* the earliest possible date */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_iso_weeks_including_year_boundaries);
    RUN_TEST(test_new_quarter_and_full_moons_of_2026);
    RUN_TEST(test_illumination_between_phases);
    RUN_TEST(test_waxing_before_full_and_waning_after);
    RUN_TEST(test_easter_sunday_including_the_earliest_and_latest_dates);
    return UNITY_END();
}
