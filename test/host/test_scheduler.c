#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "scheduler.h"
#include "unity.h"

/* Europe/Prague, the default time zone (AGENTS.md §1). */
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

void setUp(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
}

void tearDown(void) {}

/* UTC seconds for a UTC calendar time (timegm is not standard C). */
static time_t utc(int y, int mo, int d, int h, int mi, int s)
{
    long days = 0;
    for (int yy = 1970; yy < y; yy++) {
        days += (yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0)) ? 366 : 365;
    }
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    days += cum[mo - 1] + d - 1;
    if (mo > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) {
        days++;
    }
    return (time_t)(((days * 24 + h) * 60 + mi) * 60 + s);
}

static sched_wake_t next(time_t now, int display, int sensors)
{
    sched_input_t in = { .now = now, .display_every_min = display, .sensors_every_min = sensors };
    return scheduler_next_wake(&in);
}

static void test_next_minute_for_every_minute_updates(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 0, 30), 1, 5); /* 10:00:30 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 1, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY, w.reasons);
}

static void test_a_wake_exactly_on_a_minute_moves_to_the_next_one(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 1, 0), 1, 5);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 2, 0), w.when);
}

static void test_sensor_slots_align_to_local_five_minutes(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 4, 10), 1, 5);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 5, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
}

static void test_fifteen_minute_updates(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 7, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 15, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY, w.reasons);
}

static void test_spring_forward_skips_the_missing_hour(void)
{
    /* 2026-03-29 01:50 CET = 00:50 UTC; 02:00 local does not exist, 01:00 UTC is 03:00 CEST. */
    sched_wake_t w = next(utc(2026, 3, 29, 0, 50, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
}

static void test_fall_back_keeps_quarter_hours(void)
{
    /* 2026-10-25 02:50 CEST = 00:50 UTC; at 01:00 UTC it is 02:00 CET again. */
    sched_wake_t w = next(utc(2026, 10, 25, 0, 50, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 1, 0, 0), w.when);
    w = next(utc(2026, 10, 25, 1, 0, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 1, 15, 0), w.when);
}

static void test_is_slot_needs_a_whole_minute(void)
{
    TEST_ASSERT_TRUE(scheduler_is_slot(utc(2026, 9, 25, 8, 5, 0), 5));
    TEST_ASSERT_FALSE(scheduler_is_slot(utc(2026, 9, 25, 8, 5, 1), 5));
    TEST_ASSERT_FALSE(scheduler_is_slot(utc(2026, 9, 25, 8, 6, 0), 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_next_minute_for_every_minute_updates);
    RUN_TEST(test_a_wake_exactly_on_a_minute_moves_to_the_next_one);
    RUN_TEST(test_sensor_slots_align_to_local_five_minutes);
    RUN_TEST(test_fifteen_minute_updates);
    RUN_TEST(test_spring_forward_skips_the_missing_hour);
    RUN_TEST(test_fall_back_keeps_quarter_hours);
    RUN_TEST(test_is_slot_needs_a_whole_minute);
    return UNITY_END();
}
