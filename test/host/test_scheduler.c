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

static void test_seconds_display_wakes_every_second_and_keeps_the_minute_alarm(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .every_second = true });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_SECOND, w.reasons);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 18, 49, 0), w.alarm);
}

static void test_cycle_switch_before_the_next_minute_comes_first(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now + 15 });
    TEST_ASSERT_EQUAL_INT64(now + 15, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
    w = scheduler_next_wake(&(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5,
                                                       .cycle_at = utc(2026, 9, 25, 18, 49, 0) });
    TEST_ASSERT_EQUAL_HEX(SCHED_DISPLAY | SCHED_CYCLE, w.reasons); /* same second: both */
    w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now + 3600 });
    TEST_ASSERT_EQUAL_INT64(w.alarm, w.when);
}

static void test_an_overdue_cycle_switch_runs_in_the_next_second(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now - 30 });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
}

/* Spec §9.2: a local time that doesn't exist fires at the first valid minute after it; a repeated
 * one fires once, at its first occurrence. Prague springs forward on 29 March 2026 at 02:00 and
 * falls back on 25 October 2026 at 03:00. */
static void test_local_times_follow_the_dst_rules(void)
{
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 20, 30, 0), sched_local_to_utc(2026, 9, 25, 22 * 60 + 30));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 1, 15, 22, 0, 0), sched_local_to_utc(2026, 1, 15, 23 * 60));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), sched_local_to_utc(2026, 3, 29, 2 * 60 + 30)); /* 03:00 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 0, 30, 0), sched_local_to_utc(2026, 10, 25, 2 * 60 + 30)); /* CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 2, 0, 0), sched_local_to_utc(2026, 10, 25, 3 * 60));
}

static void test_weekly_entries_find_their_next_day(void)
{
    const unsigned weekdays = 0x1F; /* Monday to Friday */
    time_t friday_2330 = utc(2026, 9, 25, 21, 30, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 28, 21, 0, 0), sched_next_weekly(friday_2330, 23 * 60, weekdays));
    time_t friday_2200 = utc(2026, 9, 25, 20, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 21, 0, 0), sched_next_weekly(friday_2200, 23 * 60, weekdays));
    time_t friday_2300 = utc(2026, 9, 25, 21, 0, 0); /* strictly after: this one is past */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 28, 21, 0, 0), sched_next_weekly(friday_2300, 23 * 60, weekdays));
    /* a night's end */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 26, 4, 0, 0), sched_next_weekly(friday_2330, 6 * 60, 0x7F));
    TEST_ASSERT_EQUAL_INT64(0, sched_next_weekly(friday_2330, 23 * 60, 0)); /* no days: never */
}

static void test_a_weekly_entry_in_the_spring_gap_fires_at_the_first_valid_minute(void)
{
    time_t saturday_noon = utc(2026, 3, 28, 11, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), sched_next_weekly(saturday_noon, 2 * 60 + 30, 0x7F));
    time_t after_it = utc(2026, 3, 29, 1, 0, 0); /* then the next day's 02:30 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 30, 0, 30, 0), sched_next_weekly(after_it, 2 * 60 + 30, 0x7F));
}

static void test_the_next_schedule_entry_sets_the_alarm(void)
{
    time_t now = utc(2026, 9, 25, 20, 16, 0); /* 22:16 local, display every 15 min: next slot 22:30 */
    sched_input_t in = { .now = now, .display_every_min = 15, .sensors_every_min = 30,
                         .schedule_at = utc(2026, 9, 25, 20, 20, 0) };
    sched_wake_t w = scheduler_next_wake(&in);
    TEST_ASSERT_EQUAL_INT64(in.schedule_at, w.alarm);
    TEST_ASSERT_EQUAL_INT64(in.schedule_at, w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_ENTRY, w.reasons);
    in.schedule_at = utc(2026, 9, 25, 21, 0, 0); /* after the slot: the slot comes first */
    w = scheduler_next_wake(&in);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 20, 30, 0), w.alarm);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
}

static void test_a_sync_between_display_slots_wakes_the_board(void)
{
    /* updates every 15 min (05:30 next), a sync at 05:25 local */
    time_t now = sched_local_to_utc(2026, 7, 1, 5 * 60 + 20);
    time_t sync = sched_local_to_utc(2026, 7, 1, 5 * 60 + 25);
    sched_input_t in = { .now = now, .display_every_min = 15, .sensors_every_min = 30, .sync_at = sync };
    sched_wake_t w = scheduler_next_wake(&in);
    TEST_ASSERT_EQUAL_INT64(sync, w.when);
    TEST_ASSERT_EQUAL_INT64(sync, w.alarm);
    TEST_ASSERT_EQUAL_UINT(SCHED_SYNC, w.reasons);
    in.sync_at = now; /* due already: no wake for it */
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, scheduler_next_wake(&in).reasons); /* 05:30 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_sync_between_display_slots_wakes_the_board);
    RUN_TEST(test_next_minute_for_every_minute_updates);
    RUN_TEST(test_a_wake_exactly_on_a_minute_moves_to_the_next_one);
    RUN_TEST(test_sensor_slots_align_to_local_five_minutes);
    RUN_TEST(test_fifteen_minute_updates);
    RUN_TEST(test_spring_forward_skips_the_missing_hour);
    RUN_TEST(test_fall_back_keeps_quarter_hours);
    RUN_TEST(test_is_slot_needs_a_whole_minute);
    RUN_TEST(test_seconds_display_wakes_every_second_and_keeps_the_minute_alarm);
    RUN_TEST(test_cycle_switch_before_the_next_minute_comes_first);
    RUN_TEST(test_an_overdue_cycle_switch_runs_in_the_next_second);
    RUN_TEST(test_local_times_follow_the_dst_rules);
    RUN_TEST(test_weekly_entries_find_their_next_day);
    RUN_TEST(test_a_weekly_entry_in_the_spring_gap_fires_at_the_first_valid_minute);
    RUN_TEST(test_the_next_schedule_entry_sets_the_alarm);
    return UNITY_END();
}
