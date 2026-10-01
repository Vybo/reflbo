#include <stdio.h>

#include "astro.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define TOLERANCE_S 120 /* spec §11: within 2 min of Open-Meteo */

/* Open-Meteo's own sunrise and sunset (`daily=sunrise,sunset&timezone=auto&timeformat=unixtime`), fetched on
 * 2026-10-01: the forecast API for 2026-10-01, the archive API (archive-api.open-meteo.com) for the earlier
 * dates. `offset` is the zone's real offset at local noon of the date; the archive reports today's offset
 * for every date, which doesn't touch these absolute times. */
typedef struct {
    const char *place;
    int32_t lat_e4, lon_e4;
    int year, month, day;
    int32_t offset;
    int64_t sunrise, sunset; /* UTC seconds */
} reference_t;

static const reference_t k_reference[] = {
    { "Brno", 491951, 166068, 2026, 10, 1, 7200, 1790830383, 1790872353 },
    { "Brno", 491951, 166068, 2025, 12, 21, 3600, 1766299534, 1766329064 },
    { "Brno", 491951, 166068, 2026, 3, 20, 3600, 1773982597, 1774026372 },
    { "Brno", 491951, 166068, 2026, 6, 21, 7200, 1782010102, 1782068537 },
    { "Sydney", -338688, 1512093, 2026, 6, 21, 36000, 1781989194, 1782024837 },
    { "Sydney", -338688, 1512093, 2025, 12, 21, 39600, 1766256052, 1766307927 },
    { "Quito", -1807, -784678, 2026, 3, 20, -18000, 1774005481, 1774049072 },
    /* Far west and far east: sunset on the next UTC day, sunrise on the previous one */
    { "Los Angeles", 340522, -1182437, 2026, 7, 1, -25200, 1782909916, 1782961701 },
    { "Los Angeles", 340522, -1182437, 2025, 12, 21, -28800, 1766328882, 1766364469 },
    { "Tokyo", 356762, 1396503, 2026, 6, 21, 32400, 1781983540, 1782036014 },
    { "Tokyo", 356762, 1396503, 2025, 12, 21, 32400, 1766267213, 1766302289 },
};

static void test_sunrise_and_sunset_match_open_meteo(void)
{
    for (size_t i = 0; i < sizeof(k_reference) / sizeof(k_reference[0]); i++) {
        const reference_t *r = &k_reference[i];
        astro_sun_t sun;
        astro_sun(r->year, r->month, r->day, r->offset, r->lat_e4, r->lon_e4, &sun);
        char what[64];
        snprintf(what, sizeof(what), "%s %04d-%02d-%02d", r->place, r->year, r->month, r->day);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ASTRO_NORMAL, sun.kind, what);
        TEST_ASSERT_INT64_WITHIN_MESSAGE(TOLERANCE_S, r->sunrise, sun.sunrise, what);
        TEST_ASSERT_INT64_WITHIN_MESSAGE(TOLERANCE_S, r->sunset, sun.sunset, what);
    }
}

static void test_the_day_length_is_the_time_between_them(void)
{
    for (size_t i = 0; i < sizeof(k_reference) / sizeof(k_reference[0]); i++) {
        const reference_t *r = &k_reference[i];
        astro_sun_t sun;
        astro_sun(r->year, r->month, r->day, r->offset, r->lat_e4, r->lon_e4, &sun);
        TEST_ASSERT_EQUAL_INT64(sun.sunset - sun.sunrise, sun.day_length_s);
    }
}

/* Open-Meteo marks these by sunrise = sunset = local midnight (polar night), or sunrise at midnight and
 * sunset at the next one (polar day). */
static void test_tromso_has_a_polar_night_in_december(void)
{
    astro_sun_t sun;
    astro_sun(2025, 12, 21, 3600, 696492, 189553, &sun);
    TEST_ASSERT_EQUAL_INT(ASTRO_POLAR_NIGHT, sun.kind);
    TEST_ASSERT_EQUAL_INT64(0, sun.sunrise);
    TEST_ASSERT_EQUAL_INT64(0, sun.sunset);
    TEST_ASSERT_EQUAL_INT32(0, sun.day_length_s);
}

static void test_tromso_has_a_polar_day_in_june(void)
{
    astro_sun_t sun;
    astro_sun(2026, 6, 21, 7200, 696492, 189553, &sun);
    TEST_ASSERT_EQUAL_INT(ASTRO_POLAR_DAY, sun.kind);
    TEST_ASSERT_EQUAL_INT64(0, sun.sunrise);
    TEST_ASSERT_EQUAL_INT64(0, sun.sunset);
    TEST_ASSERT_EQUAL_INT32(86400, sun.day_length_s);
}

/* The events belong to the local date asked for, wherever the zone is: Tokyo's sunrise falls on the
 * previous UTC day, and Los Angeles' sunset on the next one. */
static void test_the_events_fall_on_the_local_date(void)
{
    astro_sun_t sun;
    astro_sun(2026, 6, 21, 32400, 356762, 1396503, &sun);
    int64_t local_midnight = 1781967600; /* 2026-06-21 00:00 JST */
    TEST_ASSERT_TRUE(sun.sunrise > local_midnight && sun.sunrise < local_midnight + 86400);
    TEST_ASSERT_TRUE(sun.sunset > local_midnight && sun.sunset < local_midnight + 86400);

    astro_sun(2026, 7, 1, -25200, 340522, -1182437, &sun);
    local_midnight = 1782889200; /* 2026-07-01 00:00 PDT */
    TEST_ASSERT_TRUE(sun.sunrise > local_midnight && sun.sunrise < local_midnight + 86400);
    TEST_ASSERT_TRUE(sun.sunset > local_midnight && sun.sunset < local_midnight + 86400);
}

/* The next date's events come a day later, give or take the few minutes the season moves them. */
static void test_the_next_day_comes_a_day_later(void)
{
    astro_sun_t today, tomorrow;
    astro_sun(2026, 10, 1, 7200, 491951, 166068, &today);
    astro_sun(2026, 10, 2, 7200, 491951, 166068, &tomorrow);
    TEST_ASSERT_INT64_WITHIN(300, today.sunrise + 86400, tomorrow.sunrise);
    TEST_ASSERT_INT64_WITHIN(300, today.sunset + 86400, tomorrow.sunset);
    TEST_ASSERT_TRUE(tomorrow.day_length_s < today.day_length_s); /* autumn: the days get shorter */
}

/* The poles themselves: no division by zero, just a polar day or night by the season. */
static void test_the_poles_are_polar(void)
{
    astro_sun_t sun;
    astro_sun(2026, 6, 21, 0, 900000, 0, &sun);
    TEST_ASSERT_EQUAL_INT(ASTRO_POLAR_DAY, sun.kind);
    astro_sun(2026, 6, 21, 0, -900000, 0, &sun);
    TEST_ASSERT_EQUAL_INT(ASTRO_POLAR_NIGHT, sun.kind);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sunrise_and_sunset_match_open_meteo);
    RUN_TEST(test_the_day_length_is_the_time_between_them);
    RUN_TEST(test_tromso_has_a_polar_night_in_december);
    RUN_TEST(test_tromso_has_a_polar_day_in_june);
    RUN_TEST(test_the_events_fall_on_the_local_date);
    RUN_TEST(test_the_next_day_comes_a_day_later);
    RUN_TEST(test_the_poles_are_polar);
    return UNITY_END();
}
