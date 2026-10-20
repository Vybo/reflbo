#include <string.h>

#include "pc_session.h"
#include "unity.h"

/* The PC session's rules (spec §12a.4, D44): what a session switches when it starts, when silence means a
 * reconnect and when the end, and what comes back then: only what the owner hasn't changed by hand. */

#define MODE_TIMES 0
#define MODE_INTERVAL 1
#define MODE_ALWAYS 2

static pc_session_t s;
static const pc_session_options_t k_both = { .auto_always = true, .preset = "pc" };

void setUp(void)
{
    pc_session_init(&s);
}

void tearDown(void) {}

static void test_a_session_switches_to_always_and_the_pc_preset(void)
{
    TEST_ASSERT_TRUE(pc_session_may_start(&k_both, MODE_TIMES == MODE_ALWAYS));
    pc_start_t a = pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    TEST_ASSERT_TRUE(a.to_always);
    TEST_ASSERT_EQUAL_STRING("pc", a.select);
    TEST_ASSERT_TRUE(s.active);
    TEST_ASSERT_TRUE(pc_session_keeps_wifi(&s));
}

static void test_the_end_brings_back_the_mode_and_the_preset(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    pc_end_t e = pc_session_end(&s, true, "pc");
    TEST_ASSERT_TRUE(e.restore_mode);
    TEST_ASSERT_EQUAL(MODE_TIMES, e.mode);
    TEST_ASSERT_EQUAL_STRING("weather", e.select);
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_FALSE(pc_session_keeps_wifi(&s));
}

static void test_a_preset_chosen_by_hand_stays(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    pc_end_t e = pc_session_end(&s, true, "rain"); /* KEY moved on from the PC preset */
    TEST_ASSERT_TRUE(e.restore_mode);
    TEST_ASSERT_EQUAL_STRING("", e.select);
}

static void test_always_turned_off_by_hand_stays_off(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    pc_end_t e = pc_session_end(&s, false, "pc"); /* BOOT double left always */
    TEST_ASSERT_FALSE(e.restore_mode);
    TEST_ASSERT_EQUAL_STRING("weather", e.select);
}

static void test_the_owners_own_always_isnt_undone(void)
{
    pc_start_t a = pc_session_start(&s, &k_both, true, MODE_ALWAYS, "energy", 1000);
    TEST_ASSERT_FALSE(a.to_always);
    pc_end_t e = pc_session_end(&s, true, "pc");
    TEST_ASSERT_FALSE(e.restore_mode);
    TEST_ASSERT_EQUAL_STRING("energy", e.select);
}

static void test_without_a_preset_nothing_is_selected_or_restored(void)
{
    const pc_session_options_t only_always = { .auto_always = true, .preset = "" };
    pc_start_t a = pc_session_start(&s, &only_always, false, MODE_TIMES, "weather", 1000);
    TEST_ASSERT_EQUAL_STRING("", a.select);
    TEST_ASSERT_EQUAL_STRING("", pc_session_end(&s, true, "weather").select);
}

static void test_a_pc_preset_already_on_screen_isnt_restored_to_itself(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "pc", 1000);
    TEST_ASSERT_EQUAL_STRING("", pc_session_end(&s, true, "pc").select);
}

static void test_without_switching_to_always_a_session_needs_always(void)
{
    const pc_session_options_t preset_only = { .auto_always = false, .preset = "pc" };
    TEST_ASSERT_FALSE(pc_session_may_start(&preset_only, false)); /* it would end with the sync */
    TEST_ASSERT_TRUE(pc_session_may_start(&preset_only, true));
    pc_start_t a = pc_session_start(&s, &preset_only, true, MODE_ALWAYS, "weather", 1000);
    TEST_ASSERT_FALSE(a.to_always);
    TEST_ASSERT_EQUAL_STRING("pc", a.select);
}

static void test_the_cycle_pauses_while_the_session_preset_shows(void)
{
    TEST_ASSERT_FALSE(pc_session_pauses_cycle(&s, "pc")); /* no session */
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    TEST_ASSERT_TRUE(pc_session_pauses_cycle(&s, "pc"));
    TEST_ASSERT_FALSE(pc_session_pauses_cycle(&s, "rain"));
}

static void test_silence_brings_a_reconnect_then_the_end(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 0);
    pc_session_heard(&s, 1000);
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 1000, 5999)); /* 1 s interval: quiet for 5 s at least */
    TEST_ASSERT_EQUAL(PC_RECONNECT, pc_session_check(&s, 1000, 6001));
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 1000, 8000)); /* reconnecting: once */
    TEST_ASSERT_EQUAL(PC_END, pc_session_check(&s, 1000, 16001));    /* about 15 s after the PC went quiet */
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 0);
    pc_session_heard(&s, 1000);
    pc_session_check(&s, 1000, 6001);
    pc_session_heard(&s, 7000);                                         /* the reconnect worked */
    TEST_ASSERT_EQUAL(PC_RECONNECT, pc_session_check(&s, 1000, 12001)); /* a new silence gets its own reconnect */
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 1000, 16001));
    TEST_ASSERT_EQUAL(PC_END, pc_session_check(&s, 1000, 22001));
}

static void test_a_slow_interval_waits_longer(void)
{
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 0);
    pc_session_heard(&s, 0);
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 10000, 29999)); /* three intervals of 10 s */
    TEST_ASSERT_EQUAL(PC_RECONNECT, pc_session_check(&s, 10000, 30001));
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 10000, 59999));
    TEST_ASSERT_EQUAL(PC_END, pc_session_check(&s, 10000, 60001));
}

/* What the files keep while a session runs: the mode and the preset from before, so a restart in the middle of one
 * gives back what the owner had (spec §12a.4). NULL: the preset as it is. */
static void test_the_files_keep_what_the_owner_had(void)
{
    TEST_ASSERT_EQUAL(MODE_TIMES, pc_session_saved_mode(&s, MODE_TIMES)); /* no session: as it is */
    TEST_ASSERT_NULL(pc_session_saved_preset(&s, "pc"));
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    TEST_ASSERT_EQUAL(MODE_TIMES, pc_session_saved_mode(&s, MODE_ALWAYS)); /* the session's `always` isn't saved */
    TEST_ASSERT_EQUAL_STRING("weather", pc_session_saved_preset(&s, "pc"));
    TEST_ASSERT_NULL(pc_session_saved_preset(&s, "rain")); /* KEY moved on: the owner's choice is saved */
    TEST_ASSERT_EQUAL(MODE_INTERVAL, pc_session_saved_mode(&s, MODE_INTERVAL)); /* a mode chosen by hand */
    pc_session_start(&s, &k_both, true, MODE_ALWAYS, "pc", 1000); /* the owner's own `always`, the PC preset shown */
    TEST_ASSERT_EQUAL(MODE_ALWAYS, pc_session_saved_mode(&s, MODE_ALWAYS));
    TEST_ASSERT_NULL(pc_session_saved_preset(&s, "pc"));
}

/* A schedule's night that comes due during a session starts at its end, while the night's own end is still to come
 * (spec §12a.4). */
static void test_a_night_due_during_a_session_comes_at_its_end(void)
{
    pc_session_defer_night(&s, 1000000); /* no session: nothing is kept */
    TEST_ASSERT_EQUAL_INT64(0, pc_session_take_night(&s, 900000));
    pc_session_start(&s, &k_both, false, MODE_TIMES, "weather", 1000);
    pc_session_defer_night(&s, 1000000); /* 23:00, until 06:00 */
    TEST_ASSERT_EQUAL_INT64(1000000, pc_session_take_night(&s, 900000));
    TEST_ASSERT_EQUAL_INT64(0, pc_session_take_night(&s, 900000)); /* once */
    pc_session_defer_night(&s, 1000000);
    TEST_ASSERT_EQUAL_INT64(0, pc_session_take_night(&s, 1000000)); /* its end has come: none */
    pc_session_defer_night(&s, 2000000);
    pc_session_defer_night(&s, 1500000); /* two: the one that ends last */
    TEST_ASSERT_EQUAL_INT64(2000000, pc_session_take_night(&s, 900000));
}

static void test_no_session_has_nothing_to_check(void)
{
    TEST_ASSERT_EQUAL(PC_STEADY, pc_session_check(&s, 1000, 1000000));
    pc_end_t e = pc_session_end(&s, true, "pc");
    TEST_ASSERT_FALSE(e.restore_mode);
    TEST_ASSERT_EQUAL_STRING("", e.select);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_session_switches_to_always_and_the_pc_preset);
    RUN_TEST(test_the_end_brings_back_the_mode_and_the_preset);
    RUN_TEST(test_a_preset_chosen_by_hand_stays);
    RUN_TEST(test_always_turned_off_by_hand_stays_off);
    RUN_TEST(test_the_owners_own_always_isnt_undone);
    RUN_TEST(test_without_a_preset_nothing_is_selected_or_restored);
    RUN_TEST(test_a_pc_preset_already_on_screen_isnt_restored_to_itself);
    RUN_TEST(test_without_switching_to_always_a_session_needs_always);
    RUN_TEST(test_the_cycle_pauses_while_the_session_preset_shows);
    RUN_TEST(test_silence_brings_a_reconnect_then_the_end);
    RUN_TEST(test_a_slow_interval_waits_longer);
    RUN_TEST(test_the_files_keep_what_the_owner_had);
    RUN_TEST(test_a_night_due_during_a_session_comes_at_its_end);
    RUN_TEST(test_no_session_has_nothing_to_check);
    return UNITY_END();
}
