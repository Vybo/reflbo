#include "gesture.h"
#include "unity.h"

static gesture_recogniser_t s_g;
static const gesture_config_t k_plain = { .long_ms = 1000, .double_enabled = false };
static const gesture_config_t k_double = { .long_ms = 1000, .double_enabled = true };

void setUp(void)
{
    gesture_init(&s_g, k_plain);
}

void tearDown(void) {}

static void test_short_press_fires_on_release_without_double(void)
{
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1000));
    TEST_ASSERT_TRUE(gesture_busy(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1100));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
    TEST_ASSERT_EQUAL_UINT32(GESTURE_NO_DEADLINE, gesture_deadline(&s_g));
}

static void test_short_press_waits_out_the_double_window(void)
{
    gesture_set_config(&s_g, k_double);
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1100));
    TEST_ASSERT_EQUAL_UINT32(1400, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1399));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1400));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_second_press_in_the_window_is_a_double(void)
{
    gesture_set_config(&s_g, k_double);
    gesture_update(&s_g, true, 1000);
    gesture_update(&s_g, false, 1100);
    TEST_ASSERT_EQUAL(GESTURE_DOUBLE, gesture_update(&s_g, true, 1250));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1300));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_two_presses_without_double_are_two_shorts(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1100));
    gesture_update(&s_g, true, 1250);
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1300));
}

static void test_long_press_fires_while_held_and_release_is_silent(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL_UINT32(2000, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1999));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 2000));
    TEST_ASSERT_EQUAL_UINT32(GESTURE_NO_DEADLINE, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 2500));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_boot_long_press_can_take_three_seconds(void)
{
    gesture_set_config(&s_g, (gesture_config_t){ .long_ms = 3000, .double_enabled = false });
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 2000));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 4000));
}

static void test_bounces_inside_the_debounce_time_are_ignored(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1005));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1008));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1030)); /* recheck: still pressed */
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1200));
}

static void test_tap_shorter_than_debounce_is_caught_at_recheck(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1020));
    TEST_ASSERT_EQUAL_UINT32(1030, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1030));
}

static void test_times_wrap_around(void)
{
    gesture_update(&s_g, true, UINT32_MAX - 500);
    TEST_ASSERT_EQUAL_UINT32(499, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 100));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 499));
}

static void test_wake_tap_is_a_short_press(void)
{
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_tap(&s_g, 250));
    gesture_set_config(&s_g, k_double);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_tap(&s_g, 5000));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 5300));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_fires_on_release_without_double);
    RUN_TEST(test_short_press_waits_out_the_double_window);
    RUN_TEST(test_second_press_in_the_window_is_a_double);
    RUN_TEST(test_two_presses_without_double_are_two_shorts);
    RUN_TEST(test_long_press_fires_while_held_and_release_is_silent);
    RUN_TEST(test_boot_long_press_can_take_three_seconds);
    RUN_TEST(test_bounces_inside_the_debounce_time_are_ignored);
    RUN_TEST(test_tap_shorter_than_debounce_is_caught_at_recheck);
    RUN_TEST(test_times_wrap_around);
    RUN_TEST(test_wake_tap_is_a_short_press);
    return UNITY_END();
}
