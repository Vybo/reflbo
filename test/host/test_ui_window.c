#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "astro.h"
#include "ui_window.h"
#include "unity.h"
#include "util_time.h"

/* Europe/Prague, the default time zone (AGENTS.md §1), and Brno, the default location. */
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
#define BRNO_LAT 491951
#define BRNO_LON 166068
#define SVALBARD_LAT 782232 /* Longyearbyen: a polar day in June, a polar night in December */
#define SVALBARD_LON 156267
#define REYKJAVIK_LAT 641466 /* sunrise before 03:00 in June */
#define REYKJAVIK_LON -219426

static char s_err[96];

void setUp(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    s_err[0] = '\0';
}

void tearDown(void) {}

/* A local time that exists once (not in a DST change's hour). */
static time_t local_time(int y, int mo, int d, int h, int mi)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
    return mktime(&t);
}

/* UTC seconds for a UTC calendar time. */
static time_t utc(int y, int mo, int d, int h, int mi)
{
    return (time_t)(util_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60);
}

/* The sun at a place on a local date, as the device computes it. */
static astro_sun_t sun(int y, int mo, int d, int32_t lat, int32_t lon)
{
    int32_t offset = (int32_t)(utc(y, mo, d, 12, 0) - local_time(y, mo, d, 12, 0));
    astro_sun_t s;
    astro_sun(y, mo, d, offset, lat, lon, &s);
    return s;
}

static ui_window_t window(const char *from, const char *until, int days)
{
    ui_window_t w;
    TEST_ASSERT_TRUE_MESSAGE(ui_window_make(from, until, days, &w, s_err, sizeof(s_err)), s_err);
    return w;
}

/* Open or not at `now` at a place, and when that changes. */
static void assert_state_at(const ui_window_t *w, time_t now, int32_t lat, int32_t lon, bool open, time_t change)
{
    time_t got = -1;
    TEST_ASSERT_EQUAL(open, ui_window_open(w, now, lat, lon, &got));
    TEST_ASSERT_EQUAL_INT64((int64_t)change, (int64_t)got);
}

static void assert_state(const ui_window_t *w, time_t now, bool open, time_t change)
{
    assert_state_at(w, now, BRNO_LAT, BRNO_LON, open, change);
}

/* A bound is a local time, or sunrise or sunset with an offset of 1-180 minutes, and writes back as it came. */
static void test_bounds_read_and_write_back(void)
{
    static const struct {
        const char *text;
        int16_t bound;
    } k_cases[] = {
        { "00:00", 0 }, { "07:00", 420 }, { "23:59", 1439 },
        { "sunrise", UI_BOUND_SUNRISE }, { "sunset", UI_BOUND_SUNSET },
        { "sunrise-15", UI_BOUND_SUNRISE - 15 }, { "sunset+30", UI_BOUND_SUNSET + 30 },
        { "sunrise+180", UI_BOUND_SUNRISE + 180 }, { "sunset-180", UI_BOUND_SUNSET - 180 },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        int16_t b = -1;
        TEST_ASSERT_TRUE_MESSAGE(ui_bound_parse(k_cases[i].text, &b), k_cases[i].text);
        TEST_ASSERT_EQUAL_INT16(k_cases[i].bound, b);
        char back[UI_BOUND_TEXT_LEN];
        ui_bound_format(b, back, sizeof(back));
        TEST_ASSERT_EQUAL_STRING(k_cases[i].text, back);
    }
}

static void test_bad_bounds_are_refused(void)
{
    static const char *const k_bad[] = { "", "7:00", "24:00", "12:60", "07:00+5", "dawn", "Sunrise", "sunrise+0",
                                         "sunrise+", "sunset+181", "sunrise-181", "sunset 30", "sunset+3x",
                                         "sunrise--5", "sunset+0030" };
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        int16_t b;
        TEST_ASSERT_FALSE_MESSAGE(ui_bound_parse(k_bad[i], &b), k_bad[i]);
    }
}

/* A window is made from its texts as presets.json and the console give them, or refused with a reason. */
static void test_a_window_is_made_or_refused(void)
{
    ui_window_t w;
    TEST_ASSERT_TRUE(ui_window_make("sunrise", "sunset-30", 127, &w, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(127, w.days);
    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE, w.from);
    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNSET - 30, w.until);
    static const struct {
        const char *from, *until;
        int days;
        const char *why;
    } k_bad[] = {
        { "dawn", "sunset", 127, "from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
        { "07:00", "sunset+200", 127, "until must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
        { "sunset", "sunset", 127, "from and until can't be the same" },
        { "07:00", "07:00", 127, "from and until can't be the same" },
        { "07:00", "22:00", 0, "days must be 1-127" },
        { "07:00", "22:00", 128, "days must be 1-127" },
        { "07:00", "22:00", -1, "days must be 1-127" },
    };
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        TEST_ASSERT_FALSE(ui_window_make(k_bad[i].from, k_bad[i].until, k_bad[i].days, &w, s_err, sizeof(s_err)));
        TEST_ASSERT_EQUAL_STRING(k_bad[i].why, s_err);
    }
}

/* No window (days 0): always open, nothing ever changes. */
static void test_no_window_is_always_open(void)
{
    ui_window_t none = { 0 };
    assert_state(&none, local_time(2026, 10, 9, 3, 0), true, 0);
}

static void test_a_clock_window(void)
{
    ui_window_t w = window("07:00", "22:00", 127);
    assert_state(&w, local_time(2026, 10, 9, 6, 59), false, local_time(2026, 10, 9, 7, 0));
    assert_state(&w, local_time(2026, 10, 9, 7, 0), true, local_time(2026, 10, 9, 22, 0));
    assert_state(&w, local_time(2026, 10, 9, 22, 0), false, local_time(2026, 10, 10, 7, 0));
}

/* Until at or before from: the window closes the next day. */
static void test_a_window_across_midnight(void)
{
    ui_window_t w = window("22:00", "06:00", 127);
    assert_state(&w, local_time(2026, 10, 9, 23, 30), true, local_time(2026, 10, 10, 6, 0));
    assert_state(&w, local_time(2026, 10, 10, 5, 59), true, local_time(2026, 10, 10, 6, 0));
    assert_state(&w, local_time(2026, 10, 10, 6, 0), false, local_time(2026, 10, 10, 22, 0));
}

/* Days are the days a window opens on: Friday's night runs into Saturday morning. */
static void test_days_are_the_days_it_opens_on(void)
{
    ui_window_t w = window("22:00", "06:00", 1 << 4); /* Fridays; 2026-10-09 is one */
    assert_state(&w, local_time(2026, 10, 9, 23, 0), true, local_time(2026, 10, 10, 6, 0));
    assert_state(&w, local_time(2026, 10, 10, 5, 0), true, local_time(2026, 10, 10, 6, 0));
    assert_state(&w, local_time(2026, 10, 10, 23, 0), false, local_time(2026, 10, 16, 22, 0));
}

/* Sun bounds are that day's sunrise and sunset at the location, with their offsets. */
static void test_a_sun_window_follows_the_sun(void)
{
    astro_sun_t today = sun(2026, 10, 9, BRNO_LAT, BRNO_LON), tomorrow = sun(2026, 10, 10, BRNO_LAT, BRNO_LON);
    TEST_ASSERT_EQUAL(ASTRO_NORMAL, today.kind);
    ui_window_t day = window("sunrise", "sunset", 127);
    assert_state(&day, (time_t)today.sunrise - 1, false, (time_t)today.sunrise);
    assert_state(&day, (time_t)today.sunrise, true, (time_t)today.sunset);
    assert_state(&day, (time_t)today.sunset, false, (time_t)tomorrow.sunrise);
    ui_window_t shifted = window("sunrise+30", "sunset-45", 127);
    assert_state(&shifted, (time_t)today.sunrise + 1799, false, (time_t)today.sunrise + 1800);
    assert_state(&shifted, (time_t)today.sunrise + 1800, true, (time_t)today.sunset - 2700);
}

static void test_sunset_to_sunrise_spans_the_night(void)
{
    astro_sun_t today = sun(2026, 10, 9, BRNO_LAT, BRNO_LON), tomorrow = sun(2026, 10, 10, BRNO_LAT, BRNO_LON);
    ui_window_t night = window("sunset", "sunrise", 127);
    assert_state(&night, local_time(2026, 10, 9, 23, 0), true, (time_t)tomorrow.sunrise);
    assert_state(&night, local_time(2026, 10, 9, 12, 0), false, (time_t)today.sunset);
}

/* A sun bound can fall on the day before the window's: the window opened on tomorrow is open late today. */
static void test_a_window_can_start_the_evening_before_its_day(void)
{
    setenv("TZ", "GMT0", 1); /* Iceland */
    tzset();
    astro_sun_t day13 = sun(2026, 6, 13, REYKJAVIK_LAT, REYKJAVIK_LON);
    TEST_ASSERT_EQUAL(ASTRO_NORMAL, day13.kind);
    time_t opens = (time_t)day13.sunrise - 180 * 60; /* before midnight, on the 12th */
    TEST_ASSERT_TRUE(opens < local_time(2026, 6, 13, 0, 0));
    ui_window_t w = window("sunrise-180", "03:00", 127);
    assert_state_at(&w, local_time(2026, 6, 12, 8, 34), REYKJAVIK_LAT, REYKJAVIK_LON, false, opens);
    assert_state_at(&w, opens, REYKJAVIK_LAT, REYKJAVIK_LON, true, local_time(2026, 6, 13, 3, 0));
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    ui_window_t polar = window("sunrise-180", "22:00", 127); /* a polar day's sunrise is 00:00: 21:00 the day before */
    time_t change = -1;
    TEST_ASSERT_TRUE(ui_window_open(&polar, local_time(2026, 6, 21, 23, 0), SVALBARD_LAT, SVALBARD_LON, &change));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change); /* each day's window starts before the last one closes */
}

/* A time a DST change skips counts from the next valid minute; a repeated one from its first occurrence (§9.2). */
static void test_dst_changes(void)
{
    ui_window_t w = window("02:30", "03:30", 127);
    /* 2026-03-29: 02:00 CET becomes 03:00 CEST, so 02:30 is 03:00 (01:00 UTC); 03:30 CEST is 01:30 UTC. */
    assert_state(&w, utc(2026, 3, 29, 0, 59), false, utc(2026, 3, 29, 1, 0));
    assert_state(&w, utc(2026, 3, 29, 1, 0), true, utc(2026, 3, 29, 1, 30));
    /* 2026-10-25: 03:00 CEST becomes 02:00 CET; 02:30 is its first (00:30 UTC), 03:30 is CET (02:30 UTC). */
    assert_state(&w, utc(2026, 10, 25, 0, 30), true, utc(2026, 10, 25, 2, 30));
    /* A window inside the spring gap doesn't exist that night: both ends are 03:00 CEST, it is empty, not a day
     * long; the next night it is 02:00-02:30 CEST (00:00-00:30 UTC). */
    ui_window_t gap = window("02:00", "02:30", 127);
    assert_state(&gap, utc(2026, 3, 29, 1, 30), false, utc(2026, 3, 30, 0, 0));
}

/* Where the sun doesn't rise or set, sunrise and sunset fall back: a polar day is all sunrise-to-sunset, a polar
 * night all sunset-to-sunrise. A window open for longer than the days it can see reports no change. */
static void test_polar_days_and_nights(void)
{
    TEST_ASSERT_EQUAL(ASTRO_POLAR_DAY, sun(2026, 6, 21, SVALBARD_LAT, SVALBARD_LON).kind);
    TEST_ASSERT_EQUAL(ASTRO_POLAR_NIGHT, sun(2026, 12, 21, SVALBARD_LAT, SVALBARD_LON).kind);
    ui_window_t day = window("sunrise", "sunset", 127), night = window("sunset", "sunrise", 127);
    time_t june = local_time(2026, 6, 21, 3, 0), december = local_time(2026, 12, 21, 12, 0), change = -1;
    TEST_ASSERT_TRUE(ui_window_open(&day, june, SVALBARD_LAT, SVALBARD_LON, &change));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
    TEST_ASSERT_FALSE(ui_window_open(&night, june, SVALBARD_LAT, SVALBARD_LON, &change));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
    TEST_ASSERT_FALSE(ui_window_open(&day, december, SVALBARD_LAT, SVALBARD_LON, &change));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
    TEST_ASSERT_TRUE(ui_window_open(&night, december, SVALBARD_LAT, SVALBARD_LON, &change));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
}

/* What `preset list` prints: the window, its days when not every day, and its state now. */
static void test_the_console_text(void)
{
    char text[64];
    ui_window_t solar = window("sunrise", "sunset-30", 127), fridays = window("22:00", "06:00", (1 << 4) | 1);
    ui_window_text(&solar, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("sunrise - sunset-30", text);
    ui_window_text(&fridays, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("22:00 - 06:00 on Mo Fr", text);
    ui_window_t clock = window("07:00", "22:00", 127);
    ui_window_state_text(&clock, local_time(2026, 10, 9, 12, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("open until 22:00", text);
    ui_window_state_text(&clock, local_time(2026, 10, 9, 23, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("closed until Sat 07:00", text);
    ui_window_state_text(&fridays, local_time(2026, 10, 10, 12, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("closed until Mon 22:00", text);
    ui_window_state_text(&clock, 0, BRNO_LAT, BRNO_LON, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("open: the clock isn't set", text);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bounds_read_and_write_back);
    RUN_TEST(test_bad_bounds_are_refused);
    RUN_TEST(test_a_window_is_made_or_refused);
    RUN_TEST(test_no_window_is_always_open);
    RUN_TEST(test_a_clock_window);
    RUN_TEST(test_a_window_across_midnight);
    RUN_TEST(test_days_are_the_days_it_opens_on);
    RUN_TEST(test_a_sun_window_follows_the_sun);
    RUN_TEST(test_sunset_to_sunrise_spans_the_night);
    RUN_TEST(test_a_window_can_start_the_evening_before_its_day);
    RUN_TEST(test_dst_changes);
    RUN_TEST(test_polar_days_and_nights);
    RUN_TEST(test_the_console_text);
    return UNITY_END();
}
