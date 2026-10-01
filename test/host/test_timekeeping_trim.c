#include "timekeeping_trim.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define HOUR_MS 3600000LL
#define DAY_MS (24 * HOUR_MS)
#define T0 1790859600000LL /* 2026-10-01 13:00 UTC */

static rtc_trim_t trimmed(int offset)
{
    rtc_trim_t t;
    timekeeping_trim_init(&t);
    t.offset = (int8_t)offset;
    timekeeping_trim_set(&t, T0);
    return t;
}

/* M2 measured this board's RTC at about 3.4 s a day slow (AGENTS.md gotcha 7). */
static void test_the_boards_slow_crystal_gets_minus_nine(void)
{
    rtc_trim_t t = trimmed(0);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&t, T0 + DAY_MS, -3400));
    TEST_ASSERT_EQUAL_INT(-9, t.offset); /* -39.35 ppm / 4.34 ppm */
    TEST_ASSERT_EQUAL_INT32(-39352, t.drift_ppb);
}

static void test_a_trimmed_clock_keeps_its_offset(void)
{
    rtc_trim_t t = trimmed(-9);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0 + DAY_MS, -25)); /* what is left after the trim */
    TEST_ASSERT_EQUAL_INT(-9, t.offset);
    TEST_ASSERT_EQUAL_INT32(-289, t.drift_ppb);
}

static void test_a_fast_crystal_gets_a_positive_offset(void)
{
    rtc_trim_t t = trimmed(0);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&t, T0 + DAY_MS, 2000)); /* +23.1 ppm */
    TEST_ASSERT_EQUAL_INT(5, t.offset);
}

static void test_under_twenty_hours_measures_nothing(void)
{
    rtc_trim_t t = trimmed(0);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0 + 19 * HOUR_MS, -2700));
    TEST_ASSERT_EQUAL_INT(0, t.offset);
    TEST_ASSERT_EQUAL_INT32(TRIM_NO_DRIFT, t.drift_ppb);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&t, T0 + 20 * HOUR_MS, -2833)); /* at 20 h it does */
    TEST_ASSERT_EQUAL_INT(-9, t.offset);
}

static void test_a_manual_set_measures_nothing(void)
{
    rtc_trim_t t = trimmed(-9);
    timekeeping_trim_forget(&t); /* the menu, `rtc set` or the phone: good to a second only */
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0 + DAY_MS, -900));
    TEST_ASSERT_EQUAL_INT(-9, t.offset); /* the register keeps correcting */
    TEST_ASSERT_EQUAL_INT64(0, t.set_at_ms);
}

static void test_a_clock_never_set_to_the_millisecond_measures_nothing(void)
{
    rtc_trim_t t;
    timekeeping_trim_init(&t);
    TEST_ASSERT_EQUAL_INT(0, t.offset);
    TEST_ASSERT_EQUAL_INT32(TRIM_NO_DRIFT, t.drift_ppb);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0, -3400));
    TEST_ASSERT_EQUAL_INT32(TRIM_NO_DRIFT, t.drift_ppb);
}

static void test_the_offset_stops_at_the_registers_ends(void)
{
    rtc_trim_t slow = trimmed(-60), fast = trimmed(60);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&slow, T0 + DAY_MS, -1728)); /* -20 ppm more: -64.6 steps */
    TEST_ASSERT_EQUAL_INT(TRIM_OFFSET_MIN, slow.offset);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&fast, T0 + DAY_MS, 1728));
    TEST_ASSERT_EQUAL_INT(TRIM_OFFSET_MAX, fast.offset);
}

static void test_a_wild_error_is_recorded_but_changes_nothing(void)
{
    rtc_trim_t t = trimmed(-9);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0 + DAY_MS, 30000)); /* 347 ppm: set some other way */
    TEST_ASSERT_EQUAL_INT(-9, t.offset);
    TEST_ASSERT_EQUAL_INT32(347222, t.drift_ppb);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&t, T0 + DAY_MS, -30000));
    TEST_ASSERT_EQUAL_INT(-9, t.offset);
}

static void test_half_a_step_rounds_away_from_zero(void)
{
    const int64_t span = 1000000000; /* 11.6 days: 1 ms of error is 1 ppb */
    rtc_trim_t up = trimmed(0), down = trimmed(0), under = trimmed(0), over = trimmed(0);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&up, T0 + span, 2170));
    TEST_ASSERT_EQUAL_INT(1, up.offset);
    TEST_ASSERT_TRUE(timekeeping_trim_measure(&down, T0 + span, -2170));
    TEST_ASSERT_EQUAL_INT(-1, down.offset);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&under, T0 + span, 2169));
    TEST_ASSERT_EQUAL_INT(0, under.offset);
    TEST_ASSERT_FALSE(timekeeping_trim_measure(&over, T0 + span, -2169));
    TEST_ASSERT_EQUAL_INT(0, over.offset);
}

static void test_the_record_survives_packing(void)
{
    rtc_trim_t t = { .offset = -9, .set_at_ms = T0 + 123, .drift_ppb = -39352 }, back;
    uint8_t rec[TRIM_RECORD_LEN];
    timekeeping_trim_pack(&t, rec);
    TEST_ASSERT_TRUE(timekeeping_trim_unpack(&back, rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_INT(t.offset, back.offset);
    TEST_ASSERT_EQUAL_INT64(t.set_at_ms, back.set_at_ms);
    TEST_ASSERT_EQUAL_INT32(t.drift_ppb, back.drift_ppb);

    timekeeping_trim_init(&t);
    timekeeping_trim_pack(&t, rec);
    TEST_ASSERT_TRUE(timekeeping_trim_unpack(&back, rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_INT32(TRIM_NO_DRIFT, back.drift_ppb);
    TEST_ASSERT_EQUAL_INT64(0, back.set_at_ms);
}

static void test_a_record_of_another_shape_is_refused(void)
{
    rtc_trim_t t = { .offset = -9, .set_at_ms = T0, .drift_ppb = -289 }, back;
    uint8_t rec[TRIM_RECORD_LEN];
    timekeeping_trim_pack(&t, rec);
    TEST_ASSERT_FALSE(timekeeping_trim_unpack(&back, rec, sizeof(rec) - 1));
    TEST_ASSERT_FALSE(timekeeping_trim_unpack(&back, NULL, sizeof(rec)));
    rec[0] ^= 0xFF; /* another version */
    TEST_ASSERT_FALSE(timekeeping_trim_unpack(&back, rec, sizeof(rec)));
    rec[0] ^= 0xFF;
    rec[1] = 64; /* beyond the register */
    TEST_ASSERT_FALSE(timekeeping_trim_unpack(&back, rec, sizeof(rec)));
}

static void test_the_drift_reads_as_seconds_a_day(void)
{
    rtc_trim_t t = { .drift_ppb = -39352 };
    TEST_ASSERT_EQUAL_INT(-34, timekeeping_trim_drift_s10_per_day(&t)); /* -3.4 s a day */
    t.drift_ppb = 23148;
    TEST_ASSERT_EQUAL_INT(20, timekeeping_trim_drift_s10_per_day(&t));
    t.drift_ppb = -289;
    TEST_ASSERT_EQUAL_INT(0, timekeeping_trim_drift_s10_per_day(&t));
    t.drift_ppb = TRIM_NO_DRIFT;
    TEST_ASSERT_EQUAL_INT(0, timekeeping_trim_drift_s10_per_day(&t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_boards_slow_crystal_gets_minus_nine);
    RUN_TEST(test_a_trimmed_clock_keeps_its_offset);
    RUN_TEST(test_a_fast_crystal_gets_a_positive_offset);
    RUN_TEST(test_under_twenty_hours_measures_nothing);
    RUN_TEST(test_a_manual_set_measures_nothing);
    RUN_TEST(test_a_clock_never_set_to_the_millisecond_measures_nothing);
    RUN_TEST(test_the_offset_stops_at_the_registers_ends);
    RUN_TEST(test_a_wild_error_is_recorded_but_changes_nothing);
    RUN_TEST(test_half_a_step_rounds_away_from_zero);
    RUN_TEST(test_the_record_survives_packing);
    RUN_TEST(test_a_record_of_another_shape_is_refused);
    RUN_TEST(test_the_drift_reads_as_seconds_a_day);
    return UNITY_END();
}
