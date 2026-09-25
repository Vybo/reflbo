#include <stdint.h>

#include "unity.h"
#include "util_ticks.h"

void setUp(void) {}
void tearDown(void) {}

static void test_zero_ms_needs_no_ticks(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, util_ticks_at_least(0, 10));
}

static void test_rounds_up_and_adds_the_partial_first_tick(void)
{
    /* vTaskDelay(n) can return after n - 1 full ticks, because the first tick is partial. */
    TEST_ASSERT_EQUAL_UINT32(13, util_ticks_at_least(120, 10));
    TEST_ASSERT_EQUAL_UINT32(14, util_ticks_at_least(121, 10));
    TEST_ASSERT_EQUAL_UINT32(2, util_ticks_at_least(10, 10));
    TEST_ASSERT_EQUAL_UINT32(2, util_ticks_at_least(5, 10));
    TEST_ASSERT_EQUAL_UINT32(2, util_ticks_at_least(1, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_ms_needs_no_ticks);
    RUN_TEST(test_rounds_up_and_adds_the_partial_first_tick);
    return UNITY_END();
}
