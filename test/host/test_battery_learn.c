#include <stdlib.h>

#include "battery_learn.h"
#include "unity.h"

static battery_learn_t s_l;

void setUp(void)
{
    battery_learn_start(&s_l);
}

void tearDown(void) {}

#define HOUR 3600u

/* A charge to full at t0: an hour charging, then full for a while. Returns the time after it. */
static uint32_t charge_to_full(uint32_t t0)
{
    uint32_t t = t0;
    for (int i = 0; i < 4; i++, t += 900) {
        TEST_ASSERT_FALSE(battery_learn_add(&s_l, t, 4000 + 50 * i, BATTERY_CHARGING));
    }
    for (int i = 0; i < 8; i++, t += 900) {
        TEST_ASSERT_FALSE(battery_learn_add(&s_l, t, 4200, BATTERY_FULL));
    }
    return t;
}

/* A straight discharge from 4.19 V to 3.30 V over `hours`, a reading every 15 min, the last one at
 * the critical level. Returns what the last reading returned. */
static bool discharge(uint32_t t0, int hours)
{
    bool done = false;
    int steps = hours * 4;
    for (int i = 1; i <= steps && !done; i++) {
        int mv = 4190 - (4190 - 3300) * i / steps;
        done = battery_learn_add(&s_l, t0 + (uint32_t)i * 900u, mv, BATTERY_DISCHARGING);
    }
    return done;
}

static void test_it_waits_for_a_charged_battery(void)
{
    TEST_ASSERT_EQUAL(BATTERY_LEARN_WAITING, battery_learn_state(&s_l));
    TEST_ASSERT_FALSE(discharge(1000000, 24)); /* already off the charger: not a full discharge */
    TEST_ASSERT_EQUAL(BATTERY_LEARN_WAITING, battery_learn_state(&s_l));
}

static void test_a_full_discharge_becomes_the_curve(void)
{
    uint32_t t = charge_to_full(1000000);
    TEST_ASSERT_TRUE(discharge(t, 168)); /* a week, as this board runs */
    TEST_ASSERT_EQUAL(BATTERY_LEARN_DONE, battery_learn_state(&s_l));
    uint16_t curve[BATTERY_CURVE_POINTS];
    TEST_ASSERT_TRUE(battery_learn_curve(&s_l, curve));
    TEST_ASSERT_INT_WITHIN(15, 4200, curve[20]); /* 100 %: the last reading on the charger */
    TEST_ASSERT_INT_WITHIN(15, 3300, curve[0]);  /* 0 %: the critical level */
    TEST_ASSERT_INT_WITHIN(20, 3745, curve[10]); /* halfway through the time, halfway down */
    for (int i = 1; i < BATTERY_CURVE_POINTS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(curve[i] > curve[i - 1], "the curve rises");
    }
    TEST_ASSERT_INT_WITHIN(1, 168, battery_learn_hours(&s_l));
}

static void test_a_charge_on_the_way_starts_again(void)
{
    uint32_t t = charge_to_full(1000000);
    for (int i = 1; i <= 12; i++) { /* three hours off the charger */
        TEST_ASSERT_FALSE(battery_learn_add(&s_l, t + (uint32_t)i * 900u, 4180 - 5 * i, BATTERY_DISCHARGING));
    }
    TEST_ASSERT_EQUAL(BATTERY_LEARN_RECORDING, battery_learn_state(&s_l));
    t = charge_to_full(t + 13 * 900); /* plugged in again */
    TEST_ASSERT_EQUAL(BATTERY_LEARN_WAITING, battery_learn_state(&s_l));
    TEST_ASSERT_TRUE(discharge(t, 48));
    TEST_ASSERT_INT_WITHIN(1, 48, battery_learn_hours(&s_l)); /* from the second charge */
}

static void test_a_gap_in_the_readings_starts_again(void)
{
    uint32_t t = charge_to_full(1000000);
    TEST_ASSERT_FALSE(battery_learn_add(&s_l, t + HOUR, 4150, BATTERY_DISCHARGING));
    TEST_ASSERT_FALSE(battery_learn_add(&s_l, t + 5 * HOUR, 4100, BATTERY_DISCHARGING)); /* 4 h of nothing */
    TEST_ASSERT_EQUAL(BATTERY_LEARN_WAITING, battery_learn_state(&s_l));
}

static void test_a_discharge_too_short_to_trust_gives_no_curve(void)
{
    uint32_t t = charge_to_full(1000000);
    TEST_ASSERT_FALSE(discharge(t, 6)); /* something drew far more than this board does */
    TEST_ASSERT_EQUAL(BATTERY_LEARN_FAILED, battery_learn_state(&s_l));
    uint16_t curve[BATTERY_CURVE_POINTS];
    TEST_ASSERT_FALSE(battery_learn_curve(&s_l, curve));
}

static void test_a_discharge_of_months_still_fits(void)
{
    uint32_t t = charge_to_full(1000000);
    TEST_ASSERT_TRUE(discharge(t, 24 * 90)); /* a board with the PS/SYNC rework (gotcha 23) */
    TEST_ASSERT_TRUE(battery_learn_points(&s_l) <= BATTERY_LEARN_POINTS);
    uint16_t curve[BATTERY_CURVE_POINTS];
    TEST_ASSERT_TRUE(battery_learn_curve(&s_l, curve));
    TEST_ASSERT_INT_WITHIN(20, 3745, curve[10]);
    TEST_ASSERT_INT_WITHIN(2 * 24, 24 * 90, battery_learn_hours(&s_l));
}

static void test_a_clock_set_keeps_the_session(void)
{
    uint32_t t = charge_to_full(1000000);
    TEST_ASSERT_FALSE(battery_learn_add(&s_l, t + HOUR, 4150, BATTERY_DISCHARGING));
    battery_learn_shift_time(&s_l, 86400); /* `rtc set` or SNTP moved the clock a day on */
    TEST_ASSERT_FALSE(battery_learn_add(&s_l, t + 2 * HOUR + 86400, 4140, BATTERY_DISCHARGING));
    TEST_ASSERT_EQUAL(BATTERY_LEARN_RECORDING, battery_learn_state(&s_l));
}

static void test_stopping_ends_it(void)
{
    charge_to_full(1000000);
    battery_learn_stop(&s_l);
    TEST_ASSERT_EQUAL(BATTERY_LEARN_OFF, battery_learn_state(&s_l));
    TEST_ASSERT_FALSE(battery_learn_add(&s_l, 2000000, 3300, BATTERY_DISCHARGING));
}

/* Kept in LittleFS so a restart doesn't lose a week of discharge (D21). */
static void test_a_session_survives_being_packed(void)
{
    uint32_t t = charge_to_full(1000000);
    for (int i = 1; i <= 20; i++) {
        battery_learn_add(&s_l, t + (uint32_t)i * 900u, 4180 - 3 * i, BATTERY_DISCHARGING);
    }
    uint8_t buf[BATTERY_LEARN_PACKED_MAX];
    size_t n = battery_learn_pack(&s_l, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    battery_learn_t back;
    TEST_ASSERT_TRUE(battery_learn_unpack(&back, buf, n));
    TEST_ASSERT_EQUAL_MEMORY(&s_l, &back, sizeof(back));
    buf[n / 2] ^= 0x40; /* a flipped bit */
    TEST_ASSERT_FALSE(battery_learn_unpack(&back, buf, n));
    TEST_ASSERT_FALSE(battery_learn_unpack(&back, buf, n - 1));
}

/* After a restart the charger may have let go meanwhile: a session waiting for the discharge
 * needs a fresh reading on the charger. */
static void test_a_waiting_session_waits_afresh_after_a_restart(void)
{
    charge_to_full(1000000);
    uint8_t buf[BATTERY_LEARN_PACKED_MAX];
    size_t n = battery_learn_pack(&s_l, buf, sizeof(buf));
    battery_learn_t back;
    TEST_ASSERT_TRUE(battery_learn_unpack(&back, buf, n));
    TEST_ASSERT_EQUAL(BATTERY_LEARN_WAITING, battery_learn_state(&back));
    TEST_ASSERT_EQUAL_INT(0, battery_learn_points(&back));
}

static void test_states_have_names_for_the_web_ui(void)
{
    TEST_ASSERT_EQUAL_STRING("off", battery_learn_state_name(BATTERY_LEARN_OFF));
    TEST_ASSERT_EQUAL_STRING("waiting", battery_learn_state_name(BATTERY_LEARN_WAITING));
    TEST_ASSERT_EQUAL_STRING("recording", battery_learn_state_name(BATTERY_LEARN_RECORDING));
    TEST_ASSERT_EQUAL_STRING("done", battery_learn_state_name(BATTERY_LEARN_DONE));
    TEST_ASSERT_EQUAL_STRING("failed", battery_learn_state_name(BATTERY_LEARN_FAILED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_it_waits_for_a_charged_battery);
    RUN_TEST(test_a_full_discharge_becomes_the_curve);
    RUN_TEST(test_a_charge_on_the_way_starts_again);
    RUN_TEST(test_a_gap_in_the_readings_starts_again);
    RUN_TEST(test_a_discharge_too_short_to_trust_gives_no_curve);
    RUN_TEST(test_a_discharge_of_months_still_fits);
    RUN_TEST(test_a_clock_set_keeps_the_session);
    RUN_TEST(test_stopping_ends_it);
    RUN_TEST(test_a_session_survives_being_packed);
    RUN_TEST(test_a_waiting_session_waits_afresh_after_a_restart);
    RUN_TEST(test_states_have_names_for_the_web_ui);
    return UNITY_END();
}
