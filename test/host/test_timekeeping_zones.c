#define _POSIX_C_SOURCE 200809L /* setenv, localtime_r */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timekeeping_zones.h"
#include "unity.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

/* UTC offset in minutes of a zone's POSIX string at a UTC instant, using the C library's rules
 * (newlib on the device reads the same strings). */
static int offset_min(const char *posix, time_t t)
{
    setenv("TZ", posix, 1);
    tzset();
    struct tm local;
    localtime_r(&t, &local);
    int64_t local_s = util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 +
                      local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    return (int)((local_s - (int64_t)t) / 60);
}

static void test_each_zone_has_its_winter_and_summer_offset(void)
{
    static const struct {
        const char *iana;
        int january, july; /* minutes east of UTC */
    } k_expected[] = {
        { "UTC", 0, 0 },
        { "Europe/London", 0, 60 },
        { "Europe/Prague", 60, 120 },
        { "Europe/Helsinki", 120, 180 },
        { "Europe/Moscow", 180, 180 },
        { "Asia/Dubai", 240, 240 },
        { "Asia/Kolkata", 330, 330 },
        { "Asia/Shanghai", 480, 480 },
        { "Asia/Tokyo", 540, 540 },
        { "Australia/Sydney", 660, 600 },
        { "America/New_York", -300, -240 },
        { "America/Chicago", -360, -300 },
        { "America/Denver", -420, -360 },
        { "America/Los_Angeles", -480, -420 },
    };
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    TEST_ASSERT_EQUAL_INT(sizeof(k_expected) / sizeof(k_expected[0]), count);
    time_t jan = (time_t)(util_days_from_civil(2026, 1, 15) * 86400 + 12 * 3600);
    time_t jul = (time_t)(util_days_from_civil(2026, 7, 15) * 86400 + 12 * 3600);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_STRING(k_expected[i].iana, zones[i].iana);
        TEST_ASSERT_EQUAL_INT_MESSAGE(k_expected[i].january, offset_min(zones[i].posix, jan), zones[i].iana);
        TEST_ASSERT_EQUAL_INT_MESSAGE(k_expected[i].july, offset_min(zones[i].posix, jul), zones[i].iana);
    }
}

static void test_zones_are_found_by_name(void)
{
    TEST_ASSERT_EQUAL_INT(2, timekeeping_zone_find("Europe/Prague"));
    TEST_ASSERT_EQUAL_INT(-1, timekeeping_zone_find("Mars/Olympus"));
    TEST_ASSERT_EQUAL_INT(-1, timekeeping_zone_find(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_zone_has_its_winter_and_summer_offset);
    RUN_TEST(test_zones_are_found_by_name);
    return UNITY_END();
}
