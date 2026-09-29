#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "ui_preset.h"
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
    return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static ui_schedule_entry_t preset_at(int hour, int minute, uint8_t days, uint8_t preset)
{
    return (ui_schedule_entry_t){ .at_min = (uint16_t)(hour * 60 + minute), .days = days,
                                  .action = UI_SCHED_PRESET, .preset = preset };
}

static ui_schedule_entry_t night_at(int hour, int minute, int until_hour, int until_minute)
{
    return (ui_schedule_entry_t){ .at_min = (uint16_t)(hour * 60 + minute), .days = 0x7F,
                                  .action = UI_SCHED_NIGHT, .until_min = (uint16_t)(until_hour * 60 + until_minute) };
}

/* Friday 25 September 2026; Prague is on CEST (UTC+2). */
static void test_the_next_entry_is_the_earliest_on_its_days(void)
{
    ui_schedule_t s = { .enabled = true, .count = 3 };
    s.entries[0] = preset_at(23, 0, 0x7F, 1);
    s.entries[1] = preset_at(22, 30, 0x60, 2); /* weekends only */
    s.entries[2] = preset_at(22, 45, 0x00, 3); /* no days: never */
    int index = -1;
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 21, 0, 0), ui_schedule_next(&s, utc(2026, 9, 25, 20, 0, 0), &index));
    TEST_ASSERT_EQUAL_INT(0, index);
    /* after Friday's 23:00, Saturday's 22:30 comes before Saturday's 23:00 */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 26, 20, 30, 0), ui_schedule_next(&s, utc(2026, 9, 25, 21, 0, 0), &index));
    TEST_ASSERT_EQUAL_INT(1, index);
    ui_schedule_t empty = { .enabled = true };
    TEST_ASSERT_EQUAL_INT64(0, ui_schedule_next(&empty, utc(2026, 9, 25, 20, 0, 0), &index));
}

/* Spec §5.4: every entry runs at its minute, even when several share one. At the same minute the
 * presets run first, in list order, and a night last, as nothing runs once it has started. */
static void test_due_entries_run_by_time_with_a_night_last(void)
{
    ui_schedule_t s = { .enabled = true, .count = 5 };
    s.entries[0] = night_at(22, 30, 6, 0);
    s.entries[1] = preset_at(22, 30, 0x7F, 1);
    s.entries[2] = preset_at(22, 0, 0x7F, 2);
    s.entries[3] = preset_at(22, 30, 0x7F, 3);
    s.entries[4] = preset_at(22, 31, 0x7F, 3); /* after `now`: not yet */
    int order[UI_SCHEDULE_MAX];
    int n = ui_schedule_due(&s, utc(2026, 9, 25, 19, 59, 30), utc(2026, 9, 25, 20, 30, 5), order);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(2, order[0]); /* 22:00 */
    TEST_ASSERT_EQUAL_INT(1, order[1]); /* 22:30, the presets in list order */
    TEST_ASSERT_EQUAL_INT(3, order[2]);
    TEST_ASSERT_EQUAL_INT(0, order[3]); /* 22:30, the night */
}

static void test_an_entry_that_ran_is_not_due_again(void)
{
    ui_schedule_t s = { .enabled = true, .count = 1 };
    s.entries[0] = preset_at(22, 30, 0x7F, 1);
    int order[UI_SCHEDULE_MAX];
    time_t ran = utc(2026, 9, 25, 20, 30, 5); /* checked 5 s after 22:30 */
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, ran, utc(2026, 9, 25, 20, 31, 0), order));
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, ran, ran, order));
    /* the next day, it is due once */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, utc(2026, 9, 26, 20, 29, 0), utc(2026, 9, 26, 20, 30, 0), order));
}

/* Spec §9.2 through the schedule: 02:30 doesn't exist on 29 March 2026 and runs at 03:00 CEST; on
 * 25 October it happens twice and runs once. */
static void test_dst_changes_run_an_entry_once(void)
{
    ui_schedule_t s = { .enabled = true, .count = 1 };
    s.entries[0] = preset_at(2, 30, 0x7F, 1);
    int order[UI_SCHEDULE_MAX];
    time_t before_gap = utc(2026, 3, 29, 0, 59, 0); /* 01:59 CET */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, before_gap, utc(2026, 3, 29, 1, 0, 10), order)); /* 03:00:10 CEST */
    time_t first = utc(2026, 10, 25, 0, 30, 0); /* 02:30 CEST */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, first - 60, first + 5, order));
    time_t second = utc(2026, 10, 25, 1, 30, 0); /* 02:30 CET, an hour later */
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, first + 5, second + 5, order));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_next_entry_is_the_earliest_on_its_days);
    RUN_TEST(test_due_entries_run_by_time_with_a_night_last);
    RUN_TEST(test_an_entry_that_ran_is_not_due_again);
    RUN_TEST(test_dst_changes_run_an_entry_once);
    return UNITY_END();
}
