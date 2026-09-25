#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "timekeeping_iso.h"
#include "unity.h"

#define FRI_2026_09_25_204805 ((time_t)1790369285)

void setUp(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void tearDown(void) {}

static void test_parses_utc(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T20:48:05Z", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
}

static void test_parses_an_offset(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T22:48:05+02:00", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T15:18:05-05:30", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
}

static void test_parses_local_time_with_the_tz_rules(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25 22:48:05", &t)); /* CEST, UTC+2 */
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-01-15T12:00", &t)); /* CET, UTC+1 */
    TEST_ASSERT_EQUAL_INT64((time_t)1768474800, t);
}

static void test_seconds_are_optional(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T20:48Z", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805 - 5, t);
}

static void test_rejects_malformed_or_impossible_input(void)
{
    const char *bad[] = { "", "garbage", "2026-13-01T00:00Z", "2026-02-30T00:00Z", "2026-09-25T25:00Z",
                          "2026-09-25T20:48:05ZZ", "2026-09-25T20:48+2", "26-09-25T20:48Z", "2026-09-25" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        time_t t = 0;
        TEST_ASSERT_FALSE_MESSAGE(timekeeping_parse_iso8601(bad[i], &t), bad[i]);
    }
    time_t t = 0;
    TEST_ASSERT_FALSE(timekeeping_parse_iso8601(NULL, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_utc);
    RUN_TEST(test_parses_an_offset);
    RUN_TEST(test_parses_local_time_with_the_tz_rules);
    RUN_TEST(test_seconds_are_optional);
    RUN_TEST(test_rejects_malformed_or_impossible_input);
    return UNITY_END();
}
