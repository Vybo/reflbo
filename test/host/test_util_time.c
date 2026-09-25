#include "unity.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

static void test_known_days_since_the_epoch(void)
{
    TEST_ASSERT_EQUAL_INT64(0, util_days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(10957, util_days_from_civil(2000, 1, 1));
    TEST_ASSERT_EQUAL_INT64(20721, util_days_from_civil(2026, 9, 25));
    TEST_ASSERT_EQUAL_INT64(21243, util_days_from_civil(2028, 2, 29));
    TEST_ASSERT_EQUAL_INT64(-1, util_days_from_civil(1969, 12, 31));
}

static void test_round_trips_every_day_from_2000_to_2100(void)
{
    for (int64_t z = util_days_from_civil(2000, 1, 1); z <= util_days_from_civil(2100, 12, 31); z++) {
        int y, m, d;
        util_civil_from_days(z, &y, &m, &d);
        TEST_ASSERT_EQUAL_INT64(z, util_days_from_civil(y, m, d));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_days_since_the_epoch);
    RUN_TEST(test_round_trips_every_day_from_2000_to_2100);
    return UNITY_END();
}
