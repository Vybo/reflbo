#include "shtc3_codec.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc_matches_the_datasheet_vectors(void)
{
    const uint8_t zero[] = { 0x00 };
    const uint8_t beef[] = { 0xBE, 0xEF };
    TEST_ASSERT_EQUAL_HEX8(0xAC, shtc3_crc8(zero, 1));
    TEST_ASSERT_EQUAL_HEX8(0x92, shtc3_crc8(beef, 2));
}

static void test_parses_the_datasheet_example(void)
{
    /* Datasheet Fig. 7: T 0x648B (CRC 0xC7) = 23.73 °C, RH 0xA133 (CRC 0x1C) = 62.97 %RH. */
    const uint8_t raw[6] = { 0x64, 0x8B, 0xC7, 0xA1, 0x33, 0x1C };
    int t, rh;
    TEST_ASSERT_TRUE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(2373, t);
    TEST_ASSERT_EQUAL_INT(6297, rh);
}

static void test_rejects_a_bad_crc(void)
{
    const uint8_t raw[6] = { 0x64, 0x8B, 0xC6, 0xA1, 0x33, 0x1C };
    int t = 1, rh = 1;
    TEST_ASSERT_FALSE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(1, t);
}

static void test_converts_the_range_ends(void)
{
    uint8_t raw[6] = { 0x00, 0x00, 0, 0xFF, 0xFF, 0 };
    raw[2] = shtc3_crc8(raw, 2);
    raw[5] = shtc3_crc8(raw + 3, 2);
    int t, rh;
    TEST_ASSERT_TRUE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(-4500, t);
    TEST_ASSERT_EQUAL_INT(10000, rh); /* 99.998 %RH rounds to 100.00 */
}

static void test_offsets_keep_humidity_within_0_and_100_percent(void)
{
    TEST_ASSERT_EQUAL_INT(4750, shtc3_offset_humidity(4500, 250));
    TEST_ASSERT_EQUAL_INT(10000, shtc3_offset_humidity(9800, 500)); /* a +5 % offset near saturation */
    TEST_ASSERT_EQUAL_INT(0, shtc3_offset_humidity(300, -500));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_matches_the_datasheet_vectors);
    RUN_TEST(test_parses_the_datasheet_example);
    RUN_TEST(test_rejects_a_bad_crc);
    RUN_TEST(test_converts_the_range_ends);
    RUN_TEST(test_offsets_keep_humidity_within_0_and_100_percent);
    return UNITY_END();
}
