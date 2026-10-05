#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "sync_plan.h"
#include "unity.h"

/* Europe/Prague, the default time zone (AGENTS.md §1). */
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

#define HM(h, m) ((h) * 60 + (m))

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

/* Brno local time on 1 and 2 October 2026, both CEST days (UTC+2). */
static time_t oct1(int h, int mi)
{
    return utc(2026, 10, 1, 0, 0, 0) + (h - 2) * 3600 + mi * 60;
}

static time_t oct2(int h, int mi)
{
    return oct1(h, mi) + 86400;
}

static sync_schedule_t at_times(int count, const uint16_t *minutes)
{
    sync_schedule_t s = { .mode = SYNC_MODE_TIMES, .time_count = (uint8_t)count, .interval_min = 60 };
    for (int i = 0; i < count; i++) {
        s.times[i] = minutes[i];
    }
    return s;
}

static sync_schedule_t every(int minutes)
{
    return (sync_schedule_t){ .mode = SYNC_MODE_INTERVAL, .interval_min = (uint16_t)minutes };
}

static void quiet(sync_schedule_t *s, int from, int to)
{
    s->quiet = true;
    s->quiet_from = (uint16_t)from;
    s->quiet_to = (uint16_t)to;
}

static const uint16_t k_half_past_five[] = { HM(5, 30) };
static const uint16_t k_three_a_day[] = { HM(6, 0), HM(12, 0), HM(18, 0) };

static void test_a_daily_time_runs_today_then_tomorrow(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    TEST_ASSERT_EQUAL_INT64(oct1(5, 30), sync_next_scheduled(&s, oct1(4, 0)));
    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), sync_next_scheduled(&s, oct1(5, 30))); /* strictly after */
}

static void test_several_times_wrap_past_midnight(void)
{
    sync_schedule_t s = at_times(3, k_three_a_day);
    TEST_ASSERT_EQUAL_INT64(oct1(18, 0), sync_next_scheduled(&s, oct1(13, 0)));
    TEST_ASSERT_EQUAL_INT64(oct2(6, 0), sync_next_scheduled(&s, oct1(19, 0)));
}

static void test_interval_slots_align_to_local_midnight(void)
{
    sync_schedule_t hourly = every(60), ninety = every(90), hundred = every(100);
    TEST_ASSERT_EQUAL_INT64(oct1(11, 0), sync_next_scheduled(&hourly, oct1(10, 20)));
    TEST_ASSERT_EQUAL_INT64(oct1(10, 30), sync_next_scheduled(&ninety, oct1(10, 20))); /* 7 × 90 min */
    TEST_ASSERT_EQUAL_INT64(oct1(23, 20), sync_next_scheduled(&hundred, oct1(23, 0)));
    TEST_ASSERT_EQUAL_INT64(oct2(0, 0), sync_next_scheduled(&hundred, oct1(23, 30))); /* the day starts again */
}

static void test_an_interval_out_of_range_is_clamped(void)
{
    sync_schedule_t fast = every(0), slow = every(5000);
    TEST_ASSERT_EQUAL_INT64(oct1(10, 30), sync_next_scheduled(&fast, oct1(10, 20))); /* 15 min */
    TEST_ASSERT_EQUAL_INT64(oct2(0, 0), sync_next_scheduled(&slow, oct1(10, 20)));   /* 1440 min */
}

static void test_always_refreshes_every_hour(void)
{
    sync_schedule_t s = { .mode = SYNC_MODE_ALWAYS };
    TEST_ASSERT_EQUAL_INT64(oct1(11, 0), sync_next_scheduled(&s, oct1(10, 20)));
    TEST_ASSERT_EQUAL_INT64(oct1(12, 0), sync_next_scheduled(&s, oct1(11, 0)));
}

static void test_manual_never_syncs_by_itself(void)
{
    sync_schedule_t s = { .mode = SYNC_MODE_MANUAL };
    sync_history_t h = { .failed_at = oct1(10, 0) };
    TEST_ASSERT_EQUAL_INT64(0, sync_next_scheduled(&s, oct1(10, 0)));
    sync_due_t due = sync_next_due(&s, &h, oct1(10, 0), false);
    TEST_ASSERT_EQUAL_INT64(0, due.at); /* no retries either */
    TEST_ASSERT_FALSE(due.retry);
}

static void test_a_time_without_any_times_never_comes(void)
{
    sync_schedule_t s = at_times(0, k_half_past_five);
    TEST_ASSERT_EQUAL_INT64(0, sync_next_scheduled(&s, oct1(4, 0)));
}

static void test_quiet_hours_move_a_time_to_their_end(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    quiet(&s, HM(23, 0), HM(6, 0));
    TEST_ASSERT_EQUAL_INT64(oct1(6, 0), sync_next_scheduled(&s, oct1(4, 0)));
}

static void test_quiet_hours_gather_interval_slots_at_their_end(void)
{
    sync_schedule_t s = every(60);
    quiet(&s, HM(23, 0), HM(6, 0));
    TEST_ASSERT_EQUAL_INT64(oct2(6, 0), sync_next_scheduled(&s, oct1(22, 30))); /* 23:00 to 05:00 wait for 06:00 */
    TEST_ASSERT_EQUAL_INT64(oct2(6, 0), sync_next_scheduled(&s, oct2(3, 10)));
    TEST_ASSERT_EQUAL_INT64(oct2(6, 0), sync_next_scheduled(&s, oct2(5, 30))); /* the slot at the end runs */
    TEST_ASSERT_EQUAL_INT64(oct2(7, 0), sync_next_scheduled(&s, oct2(6, 0)));  /* once */
}

static void test_quiet_hours_within_a_day(void)
{
    sync_schedule_t s = every(60);
    quiet(&s, HM(12, 0), HM(14, 0));
    TEST_ASSERT_EQUAL_INT64(oct1(14, 0), sync_next_scheduled(&s, oct1(11, 30)));
    TEST_ASSERT_EQUAL_INT64(oct1(15, 0), sync_next_scheduled(&s, oct1(14, 0)));
}

static void test_quiet_hours_of_no_minutes_are_none(void)
{
    sync_schedule_t s = every(60);
    quiet(&s, HM(23, 0), HM(23, 0));
    TEST_ASSERT_EQUAL_INT64(oct1(23, 0), sync_next_scheduled(&s, oct1(22, 30)));
    TEST_ASSERT_FALSE(sync_quiet_at(&s, oct1(23, 0)));
}

static void test_quiet_hours_switched_off_change_nothing(void)
{
    sync_schedule_t s = every(60);
    quiet(&s, HM(23, 0), HM(6, 0));
    s.quiet = false;
    TEST_ASSERT_EQUAL_INT64(oct1(23, 0), sync_next_scheduled(&s, oct1(22, 30)));
    TEST_ASSERT_FALSE(sync_quiet_at(&s, oct2(2, 0)));
}

static void test_quiet_hours_take_their_start_but_not_their_end(void)
{
    sync_schedule_t night = every(60), noon = every(60);
    quiet(&night, HM(23, 0), HM(6, 0));
    quiet(&noon, HM(12, 0), HM(14, 0));
    TEST_ASSERT_FALSE(sync_quiet_at(&night, oct1(22, 59)));
    TEST_ASSERT_TRUE(sync_quiet_at(&night, oct1(23, 0)));
    TEST_ASSERT_TRUE(sync_quiet_at(&night, oct2(5, 59)));
    TEST_ASSERT_FALSE(sync_quiet_at(&night, oct2(6, 0)));
    TEST_ASSERT_FALSE(sync_quiet_at(&night, oct1(12, 0)));
    TEST_ASSERT_FALSE(sync_quiet_at(&noon, oct1(11, 59)));
    TEST_ASSERT_TRUE(sync_quiet_at(&noon, oct1(12, 0)));
    TEST_ASSERT_TRUE(sync_quiet_at(&noon, oct1(13, 59)));
    TEST_ASSERT_FALSE(sync_quiet_at(&noon, oct1(14, 0)));
}

static void test_retries_climb_fifteen_thirty_sixty_minutes(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { 0 };
    sync_history_record(&h, (sync_due_t){ oct1(5, 30), false }, false, oct1(5, 31));
    sync_due_t due = sync_next_due(&s, &h, oct1(5, 31), false);
    TEST_ASSERT_EQUAL_INT64(oct1(5, 46), due.at);
    TEST_ASSERT_TRUE(due.retry);

    sync_history_record(&h, due, false, oct1(5, 47));
    due = sync_next_due(&s, &h, oct1(5, 47), false);
    TEST_ASSERT_EQUAL_INT64(oct1(6, 17), due.at);
    TEST_ASSERT_TRUE(due.retry);

    sync_history_record(&h, due, false, oct1(6, 18));
    due = sync_next_due(&s, &h, oct1(6, 18), false);
    TEST_ASSERT_EQUAL_INT64(oct1(7, 18), due.at);
    TEST_ASSERT_TRUE(due.retry);

    sync_history_record(&h, due, false, oct1(7, 19)); /* the third retry failed too */
    due = sync_next_due(&s, &h, oct1(7, 19), false);
    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
    TEST_ASSERT_FALSE(due.retry);
}

static void test_an_earlier_scheduled_sync_wins_over_a_retry(void)
{
    sync_schedule_t s = every(15);
    sync_history_t second = { .failed_at = oct1(10, 1), .retries = 1 }; /* its retry: 10:31 */
    sync_due_t due = sync_next_due(&s, &second, oct1(10, 1), false);
    TEST_ASSERT_EQUAL_INT64(oct1(10, 15), due.at);
    TEST_ASSERT_FALSE(due.retry);

    sync_history_t first = { .failed_at = oct1(10, 0) }; /* its retry: 10:15, as is the slot */
    due = sync_next_due(&s, &first, oct1(10, 0), false);
    TEST_ASSERT_EQUAL_INT64(oct1(10, 15), due.at);
    TEST_ASSERT_FALSE(due.retry); /* a tie is the scheduled sync */
}

static void test_low_battery_runs_no_retries(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { .failed_at = oct1(5, 31) };
    sync_due_t due = sync_next_due(&s, &h, oct1(5, 31), true);
    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
    TEST_ASSERT_FALSE(due.retry);
}

static void test_a_retry_inside_quiet_hours_waits_for_their_end(void)
{
    static const uint16_t ten_to_eleven[] = { HM(22, 50) };
    sync_schedule_t s = at_times(1, ten_to_eleven);
    quiet(&s, HM(23, 0), HM(6, 0));
    sync_history_t h = { .failed_at = oct1(22, 51) }; /* its retry, 23:06, is quiet */
    sync_due_t due = sync_next_due(&s, &h, oct1(22, 51), false);
    TEST_ASSERT_EQUAL_INT64(oct2(6, 0), due.at);
    TEST_ASSERT_TRUE(due.retry);
}

static void test_an_overdue_retry_is_due_now(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { .failed_at = oct1(5, 31) };
    sync_due_t due = sync_next_due(&s, &h, oct1(8, 0), false); /* a restart in between */
    TEST_ASSERT_EQUAL_INT64(oct1(8, 0), due.at);
    TEST_ASSERT_TRUE(due.retry);
}

static void test_a_failure_after_now_counts_from_now(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { .failed_at = oct2(0, 0) }; /* the clock moved back since */
    sync_due_t due = sync_next_due(&s, &h, oct1(10, 0), false);
    TEST_ASSERT_EQUAL_INT64(oct1(10, 15), due.at);
    TEST_ASSERT_TRUE(due.retry);
}

static void test_the_history_records_success_and_failures(void)
{
    sync_history_t h = { .failed_at = oct1(5, 0), .retries = 2 };
    sync_history_record(&h, (sync_due_t){ oct1(5, 30), true }, true, oct1(5, 31));
    TEST_ASSERT_EQUAL_INT64(0, h.failed_at);
    TEST_ASSERT_EQUAL_UINT8(0, h.retries);

    sync_history_record(&h, (sync_due_t){ oct1(5, 30), false }, false, oct1(5, 31)); /* scheduled */
    TEST_ASSERT_EQUAL_INT64(oct1(5, 31), h.failed_at);
    TEST_ASSERT_EQUAL_UINT8(0, h.retries);

    sync_history_record(&h, (sync_due_t){ oct1(5, 46), true }, false, oct1(5, 47)); /* a retry */
    TEST_ASSERT_EQUAL_INT64(oct1(5, 47), h.failed_at);
    TEST_ASSERT_EQUAL_UINT8(1, h.retries);

    sync_history_record(&h, (sync_due_t){ 0, false }, false, oct1(9, 0)); /* on demand: the retries start over */
    TEST_ASSERT_EQUAL_INT64(oct1(9, 0), h.failed_at);
    TEST_ASSERT_EQUAL_UINT8(0, h.retries);

    h.retries = SYNC_RETRY_COUNT;
    sync_history_record(&h, (sync_due_t){ oct1(10, 0), true }, false, oct1(10, 1));
    TEST_ASSERT_EQUAL_UINT8(SYNC_RETRY_COUNT, h.retries);
}

static void test_the_expected_interval_of_one_daily_time_is_a_day(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    TEST_ASSERT_EQUAL_UINT32(86400, sync_expected_interval_s(&s, oct1(12, 0)));
}

static void test_the_expected_interval_of_several_times_is_the_longest_gap(void)
{
    sync_schedule_t s = at_times(3, k_three_a_day);
    TEST_ASSERT_EQUAL_UINT32(12 * 3600, sync_expected_interval_s(&s, oct1(12, 0))); /* 18:00 → 06:00 */
}

static void test_the_expected_interval_of_an_interval(void)
{
    sync_schedule_t hourly = every(60), ninety = every(90), hundred = every(100);
    TEST_ASSERT_EQUAL_UINT32(3600, sync_expected_interval_s(&hourly, oct1(12, 0)));
    TEST_ASSERT_EQUAL_UINT32(5400, sync_expected_interval_s(&ninety, oct1(12, 0)));
    TEST_ASSERT_EQUAL_UINT32(6000, sync_expected_interval_s(&hundred, oct1(12, 0))); /* 23:20 → 00:00 is shorter */
}

static void test_quiet_hours_stretch_the_expected_interval(void)
{
    sync_schedule_t s = every(60);
    quiet(&s, HM(23, 0), HM(6, 0));
    TEST_ASSERT_EQUAL_UINT32(8 * 3600, sync_expected_interval_s(&s, oct1(12, 0))); /* 22:00 → 06:00 */
}

static void test_the_expected_interval_of_always_and_manual(void)
{
    sync_schedule_t always = { .mode = SYNC_MODE_ALWAYS }, manual = { .mode = SYNC_MODE_MANUAL };
    TEST_ASSERT_EQUAL_UINT32(SYNC_ALWAYS_REFRESH_MIN * 60, sync_expected_interval_s(&always, oct1(12, 0)));
    TEST_ASSERT_EQUAL_UINT32(0, sync_expected_interval_s(&manual, oct1(12, 0)));
}

static void test_wifi_is_wanted_in_always_mode_outside_quiet_hours(void)
{
    sync_schedule_t always = { .mode = SYNC_MODE_ALWAYS }, daily = at_times(1, k_half_past_five);
    quiet(&always, HM(23, 0), HM(6, 0));
    TEST_ASSERT_TRUE(sync_wifi_wanted(&always, oct1(12, 0)));
    TEST_ASSERT_FALSE(sync_wifi_wanted(&always, oct1(23, 30)));
    TEST_ASSERT_TRUE(sync_wifi_wanted(&always, oct2(6, 0)));
    TEST_ASSERT_FALSE(sync_wifi_wanted(&daily, oct1(12, 0)));
}

static void test_radar_refreshes_follow_the_frames(void)
{
    /* spec §9.3: ČHMÚ every 5 min, RainViewer every 10, a minute after each step for the frame to appear */
    TEST_ASSERT_EQUAL_INT64(oct1(12, 1), sync_radar_next(oct1(12, 0), 300));
    TEST_ASSERT_EQUAL_INT64(oct1(12, 6), sync_radar_next(oct1(12, 1), 300)); /* strictly after */
    TEST_ASSERT_EQUAL_INT64(oct1(12, 6), sync_radar_next(oct1(12, 4) + 59, 300));
    TEST_ASSERT_EQUAL_INT64(oct1(12, 11), sync_radar_next(oct1(12, 1), 600));
    TEST_ASSERT_EQUAL_INT64(oct1(12, 1), sync_radar_next(oct1(11, 51) + 1, 600));
}

static void test_hhmm_text(void)
{
    TEST_ASSERT_EQUAL_INT(330, sync_parse_hhmm("05:30"));
    TEST_ASSERT_EQUAL_INT(0, sync_parse_hhmm("00:00"));
    TEST_ASSERT_EQUAL_INT(1439, sync_parse_hhmm("23:59"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("24:00"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("05:60"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("5:30"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("05:30x"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("05-30"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm("ab:cd"));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm(""));
    TEST_ASSERT_EQUAL_INT(-1, sync_parse_hhmm(NULL));
}

static void test_a_time_in_the_spring_forward_gap_runs_after_it(void)
{
    /* 2026-03-29: at 02:00 CET (01:00 UTC) the clock jumps to 03:00 CEST; 02:30 doesn't exist. */
    static const uint16_t half_past_two[] = { HM(2, 30) };
    sync_schedule_t s = at_times(1, half_past_two);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), sync_next_scheduled(&s, utc(2026, 3, 28, 23, 0, 0)));
}

static void test_a_time_in_the_repeated_hour_runs_once(void)
{
    /* 2026-10-25: at 03:00 CEST (01:00 UTC) the clock goes back to 02:00 CET; 02:30 comes twice. */
    static const uint16_t half_past_two[] = { HM(2, 30) };
    sync_schedule_t s = at_times(1, half_past_two);
    time_t first = utc(2026, 10, 25, 0, 30, 0); /* 02:30 CEST */
    TEST_ASSERT_EQUAL_INT64(first, sync_next_scheduled(&s, utc(2026, 10, 24, 22, 0, 0)));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 26, 1, 30, 0), sync_next_scheduled(&s, first)); /* not 01:30 UTC today */
}

static void test_a_lost_clock_syncs_at_once_then_waits_between_tries(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { 0 };
    time_t t = utc(2000, 1, 1, 0, 0, 0); /* D9: the RTC restarted from its reset value */
    sync_due_t due = sync_next_due_needing(&s, &h, t, false, SYNC_NEED_TIME);
    TEST_ASSERT_EQUAL_INT64(t, due.at);
    static const int k_waits_min[] = { 15, 30, 60, 60, 60 }; /* and every hour after the third */
    for (int i = 0; i < 5; i++) {
        sync_history_record(&h, due, false, t);
        due = sync_next_due_needing(&s, &h, t, true, SYNC_NEED_TIME); /* the battery doesn't stop it */
        TEST_ASSERT_EQUAL_INT64_MESSAGE(t + k_waits_min[i] * 60, due.at, "a failed try waits");
        TEST_ASSERT_TRUE(due.retry);
        t = due.at + 40;
    }
    due = sync_next_due_needing(&s, &h, t + 5 * 3600, false, SYNC_NEED_TIME); /* overdue: now */
    TEST_ASSERT_EQUAL_INT64(t + 5 * 3600, due.at);
}

static void test_no_forecast_yet_syncs_at_once_unless_a_sync_failed(void)
{
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { 0 };
    sync_due_t due = sync_next_due_needing(&s, &h, oct1(18, 40), false, SYNC_NEED_FORECAST); /* a network saved */
    TEST_ASSERT_EQUAL_INT64(oct1(18, 40), due.at);
    TEST_ASSERT_FALSE(due.retry);
    sync_history_record(&h, due, false, oct1(18, 41)); /* then the retries and the schedule, as ever */
    due = sync_next_due_needing(&s, &h, oct1(18, 41), false, SYNC_NEED_FORECAST);
    TEST_ASSERT_EQUAL_INT64(oct1(18, 56), due.at);
    TEST_ASSERT_TRUE(due.retry);
    sync_schedule_t manual = { .mode = SYNC_MODE_MANUAL };
    h = (sync_history_t){ 0 };
    TEST_ASSERT_EQUAL_INT64(0, sync_next_due_needing(&manual, &h, oct1(18, 40), false, SYNC_NEED_FORECAST).at);
    due = sync_next_due_needing(&s, &h, oct1(18, 40), false, SYNC_NEED_NOTHING);
    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
}

/* The clock first, then `always` mode's Wi-Fi, then the first forecast, which only a sync with its Weather step on
 * brings (D35): with that step off the schedule decides, or each successful sync would start the next at once. */
static void test_only_the_weather_step_brings_the_first_forecast(void)
{
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_TIME, sync_need(false, true, false, true));
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_TIME, sync_need(false, false, false, false)); /* the time step has no switch */
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_WIFI, sync_need(true, true, false, false));
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_FORECAST, sync_need(true, false, false, true));
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_NOTHING, sync_need(true, false, true, true));
    TEST_ASSERT_EQUAL_INT(SYNC_NEED_NOTHING, sync_need(true, false, false, false));
    sync_schedule_t s = at_times(1, k_half_past_five);
    sync_history_t h = { 0 };
    sync_history_record(&h, (sync_due_t){ .at = oct1(18, 40) }, true, oct1(18, 41)); /* it brought no forecast */
    sync_due_t due = sync_next_due_needing(&s, &h, oct1(18, 41), false, sync_need(true, false, false, false));
    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
}

static void test_always_brings_wifi_back_at_once_unless_a_sync_failed(void)
{
    sync_schedule_t s = { .mode = SYNC_MODE_ALWAYS };
    sync_history_t h = { 0 };
    sync_due_t due = sync_next_due_needing(&s, &h, oct1(20, 7), false, SYNC_NEED_WIFI); /* a night ended */
    TEST_ASSERT_EQUAL_INT64(oct1(20, 7), due.at);
    TEST_ASSERT_FALSE(due.retry);
    sync_history_record(&h, due, false, oct1(20, 8)); /* the router is off: the retries, not every tick */
    due = sync_next_due_needing(&s, &h, oct1(20, 9), false, SYNC_NEED_WIFI);
    TEST_ASSERT_EQUAL_INT64(oct1(20, 23), due.at);
    TEST_ASSERT_TRUE(due.retry);
    h.retries = SYNC_RETRY_COUNT; /* after the third: the hourly refresh */
    due = sync_next_due_needing(&s, &h, oct1(20, 9), false, SYNC_NEED_WIFI);
    TEST_ASSERT_EQUAL_INT64(oct1(21, 0), due.at);
}

static void test_a_step_gets_its_limit_or_what_is_left_of_the_sync(void)
{
    int64_t deadline = (int64_t)SYNC_RADIO_MAX_MS * 1000; /* the sync started at 0 */
    TEST_ASSERT_EQUAL_INT(5000, sync_budget_ms(0, deadline, 5000));
    TEST_ASSERT_EQUAL_INT(5000, sync_budget_ms(deadline - 5000000, deadline, 10000));
    TEST_ASSERT_EQUAL_INT(SYNC_STEP_MIN_MS, sync_budget_ms(deadline - SYNC_STEP_MIN_MS * 1000, deadline, 10000));
    TEST_ASSERT_EQUAL_INT(0, sync_budget_ms(deadline - 999000, deadline, 10000)); /* skipped */
    TEST_ASSERT_EQUAL_INT(0, sync_budget_ms(deadline + 1, deadline, 10000));
}

static void test_interval_slots_on_the_fall_back_day(void)
{
    sync_schedule_t s = every(60);
    /* 02:00 CEST is 00:00 UTC; the second 02:00 (CET, 01:00 UTC) doesn't run again; 03:00 CET is 02:00 UTC. */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 2, 0, 0), sync_next_scheduled(&s, utc(2026, 10, 25, 0, 0, 0)));
    TEST_ASSERT_EQUAL_UINT32(2 * 3600, sync_expected_interval_s(&s, utc(2026, 10, 25, 10, 0, 0)));
}

/* A sync fails when one of its steps fails, but not for the house's energy (D36): it shows, starts no retry. A step
 * that kept what it had (Solcast's budget, a provider's 429) or didn't run (switched off) is no failure (D35). */
static void test_a_sync_fails_on_any_step_but_the_energy(void)
{
    uint8_t r[SYNC_STEP_COUNT];
    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
        r[i] = SYNC_STEP_OK;
    }
    TEST_ASSERT_FALSE(sync_report_failed(r));
    TEST_ASSERT_EQUAL_INT(SYNC_STEP_COUNT, sync_first_failed(r));
    r[SYNC_STEP_ENERGY] = SYNC_STEP_FAILED;
    TEST_ASSERT_FALSE(sync_report_failed(r));
    TEST_ASSERT_EQUAL_INT(SYNC_STEP_ENERGY, sync_first_failed(r)); /* Info still names it */
    r[SYNC_STEP_SOLAR] = SYNC_STEP_KEPT;
    r[SYNC_STEP_RADAR] = SYNC_STEP_NOT_RUN;
    TEST_ASSERT_FALSE(sync_report_failed(r));
    r[SYNC_STEP_SOLAR] = SYNC_STEP_FAILED;
    TEST_ASSERT_TRUE(sync_report_failed(r));
    TEST_ASSERT_EQUAL_INT(SYNC_STEP_SOLAR, sync_first_failed(r));
    r[SYNC_STEP_WIFI] = SYNC_STEP_FAILED;
    TEST_ASSERT_EQUAL_INT(SYNC_STEP_WIFI, sync_first_failed(r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_daily_time_runs_today_then_tomorrow);
    RUN_TEST(test_several_times_wrap_past_midnight);
    RUN_TEST(test_interval_slots_align_to_local_midnight);
    RUN_TEST(test_an_interval_out_of_range_is_clamped);
    RUN_TEST(test_always_refreshes_every_hour);
    RUN_TEST(test_manual_never_syncs_by_itself);
    RUN_TEST(test_a_time_without_any_times_never_comes);
    RUN_TEST(test_quiet_hours_move_a_time_to_their_end);
    RUN_TEST(test_quiet_hours_gather_interval_slots_at_their_end);
    RUN_TEST(test_quiet_hours_within_a_day);
    RUN_TEST(test_quiet_hours_of_no_minutes_are_none);
    RUN_TEST(test_quiet_hours_switched_off_change_nothing);
    RUN_TEST(test_quiet_hours_take_their_start_but_not_their_end);
    RUN_TEST(test_retries_climb_fifteen_thirty_sixty_minutes);
    RUN_TEST(test_an_earlier_scheduled_sync_wins_over_a_retry);
    RUN_TEST(test_low_battery_runs_no_retries);
    RUN_TEST(test_a_retry_inside_quiet_hours_waits_for_their_end);
    RUN_TEST(test_an_overdue_retry_is_due_now);
    RUN_TEST(test_a_lost_clock_syncs_at_once_then_waits_between_tries);
    RUN_TEST(test_no_forecast_yet_syncs_at_once_unless_a_sync_failed);
    RUN_TEST(test_only_the_weather_step_brings_the_first_forecast);
    RUN_TEST(test_always_brings_wifi_back_at_once_unless_a_sync_failed);
    RUN_TEST(test_a_step_gets_its_limit_or_what_is_left_of_the_sync);
    RUN_TEST(test_a_failure_after_now_counts_from_now);
    RUN_TEST(test_the_history_records_success_and_failures);
    RUN_TEST(test_the_expected_interval_of_one_daily_time_is_a_day);
    RUN_TEST(test_the_expected_interval_of_several_times_is_the_longest_gap);
    RUN_TEST(test_the_expected_interval_of_an_interval);
    RUN_TEST(test_quiet_hours_stretch_the_expected_interval);
    RUN_TEST(test_the_expected_interval_of_always_and_manual);
    RUN_TEST(test_wifi_is_wanted_in_always_mode_outside_quiet_hours);
    RUN_TEST(test_radar_refreshes_follow_the_frames);
    RUN_TEST(test_a_sync_fails_on_any_step_but_the_energy);
    RUN_TEST(test_hhmm_text);
    RUN_TEST(test_a_time_in_the_spring_forward_gap_runs_after_it);
    RUN_TEST(test_a_time_in_the_repeated_hour_runs_once);
    RUN_TEST(test_interval_slots_on_the_fall_back_day);
    return UNITY_END();
}
