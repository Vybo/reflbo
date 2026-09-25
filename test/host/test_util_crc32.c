#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "util_crc32.h"

void setUp(void) {}
void tearDown(void) {}

static void test_standard_check_value(void)
{
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, util_crc32(0, s, strlen(s)));
}

static void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, util_crc32(0, NULL, 0));
}

static void test_single_byte(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xE8B7BE43u, util_crc32(0, "a", 1));
}

static void test_chained_calls_match_one_pass(void)
{
    const char *s = "123456789";
    uint32_t first = util_crc32(0, s, 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, util_crc32(first, s + 4, 5));
}

static void test_frame_sized_buffers(void)
{
    static uint8_t frame[15000]; /* one 400x300 1-bpp frame */
    memset(frame, 0xFF, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX32(0x3C029422u, util_crc32(0, frame, sizeof(frame)));
    memset(frame, 0x00, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX32(0xD17AFDBEu, util_crc32(0, frame, sizeof(frame)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_check_value);
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_single_byte);
    RUN_TEST(test_chained_calls_match_one_pass);
    RUN_TEST(test_frame_sized_buffers);
    return UNITY_END();
}
