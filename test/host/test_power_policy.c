#include "power_policy.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static power_plan_t plan(power_idle_t strategy, bool tethered, bool hold, int test_cycles, power_idle_t test_mode)
{
    power_policy_input_t in = { .strategy = strategy, .tethered = tethered, .hold_awake = hold,
                                .test_cycles = test_cycles, .test_mode = test_mode };
    return power_policy(&in);
}

static void test_untethered_board_sleeps_with_its_strategy(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_LIGHT, plan(POWER_IDLE_LIGHT, false, false, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_DEEP, plan(POWER_IDLE_DEEP, false, false, 0, POWER_IDLE_LIGHT));
}

static void test_tethered_board_stays_awake(void)
{
    /* Light and deep sleep both drop the USB console (AGENTS.md gotcha 11). */
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_LIGHT, true, false, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_DEEP, true, false, 0, POWER_IDLE_LIGHT));
}

static void test_holding_awake_wins_even_during_a_sleep_test(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_DEEP, false, true, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_LIGHT, true, true, 3, POWER_IDLE_DEEP));
}

static void test_sleep_test_cycles_sleep_even_when_tethered(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_DEEP, plan(POWER_IDLE_LIGHT, true, false, 2, POWER_IDLE_DEEP));
    TEST_ASSERT_EQUAL(POWER_PLAN_LIGHT, plan(POWER_IDLE_DEEP, true, false, 1, POWER_IDLE_LIGHT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_untethered_board_sleeps_with_its_strategy);
    RUN_TEST(test_tethered_board_stays_awake);
    RUN_TEST(test_holding_awake_wins_even_during_a_sleep_test);
    RUN_TEST(test_sleep_test_cycles_sleep_even_when_tethered);
    return UNITY_END();
}
