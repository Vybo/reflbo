#include "pcf85063_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FRI_2026_09_25_204805 ((time_t)1790369285)

static void test_encodes_a_known_time(void)
{
    uint8_t r[7];
    pcf85063_encode_time(FRI_2026_09_25_204805, r);
    const uint8_t expected[7] = { 0x05, 0x48, 0x20, 0x25, 5 /* Friday */, 0x09, 0x26 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r, 7);
}

static void test_decodes_a_known_time(void)
{
    const uint8_t r[7] = { 0x05, 0x48, 0x20, 0x25, 5, 0x09, 0x26 };
    time_t t = 0;
    bool stopped = true;
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_FALSE(stopped);
}

static void test_reports_the_oscillator_stop_flag(void)
{
    const uint8_t r[7] = { 0x80, 0x00, 0x00, 0x01, 6, 0x01, 0x00 }; /* power-on reset state */
    time_t t = 0;
    bool stopped = false;
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT64(PCF85063_MIN_TIME, t);
}

static void test_round_trips_leap_day_and_range_ends(void)
{
    const time_t cases[] = { 1835438400 /* 2028-02-29T12:00Z */, PCF85063_MIN_TIME, PCF85063_MAX_TIME };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t r[7];
        time_t back = 0;
        bool stopped = true;
        pcf85063_encode_time(cases[i], r);
        TEST_ASSERT_TRUE(pcf85063_decode_time(r, &back, &stopped));
        TEST_ASSERT_EQUAL_INT64(cases[i], back);
    }
}

static void test_rejects_impossible_register_values(void)
{
    const uint8_t bad_bcd[7] = { 0x5A, 0x00, 0x00, 0x01, 6, 0x01, 0x00 };
    const uint8_t feb_30[7] = { 0x00, 0x00, 0x00, 0x30, 0, 0x02, 0x26 };
    time_t t;
    bool stopped;
    TEST_ASSERT_FALSE(pcf85063_decode_time(bad_bcd, &t, &stopped));
    TEST_ASSERT_FALSE(pcf85063_decode_time(feb_30, &t, &stopped));
}

static void test_clamps_times_outside_the_rtc_range(void)
{
    uint8_t r[7];
    time_t t;
    bool stopped;
    pcf85063_encode_time(0, r);
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_EQUAL_INT64(PCF85063_MIN_TIME, t);
}

static void test_alarm_matches_minute_hour_and_day(void)
{
    uint8_t a[5];
    pcf85063_encode_alarm((time_t)1790369340 /* 2026-09-25T20:49:00Z */, a);
    const uint8_t expected[5] = { 0x80, 0x49, 0x20, 0x25, 0x80 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, a, 5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encodes_a_known_time);
    RUN_TEST(test_decodes_a_known_time);
    RUN_TEST(test_reports_the_oscillator_stop_flag);
    RUN_TEST(test_round_trips_leap_day_and_range_ends);
    RUN_TEST(test_rejects_impossible_register_values);
    RUN_TEST(test_clamps_times_outside_the_rtc_range);
    RUN_TEST(test_alarm_matches_minute_hour_and_day);
    return UNITY_END();
}
