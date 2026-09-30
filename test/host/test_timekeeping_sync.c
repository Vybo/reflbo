#include <math.h>

#include "timekeeping_sync.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Seconds ticks against a PCF85063 that keeps true time. The app wakes when its own clock reaches
 * the next second, or when the RTC's minute alarm fires as the RTC's minute begins; it reads the
 * RTC `latency_ms` later and shows its own clock's second. `drift` is how much faster the running
 * clock goes than true time. Returns how often the second shown skipped one or went back. */
static int out_of_order(double drift, double latency_ms, int minutes)
{
    const double rate = 1.0 + drift;
    double real = 12345.0;        /* true ms at boot, in the middle of a second */
    double offset = -real * rate; /* running clock = real * rate + offset: 0 at boot */
    const double end = real + minutes * 60000.0;
    long long shown = -1;
    int bad = 0;
    while (real < end) {
        double next_tick = (floor((real * rate + offset) / 1000) + 1) * 1000; /* on the running clock */
        double tick_real = (next_tick - offset) / rate;
        double alarm_real = (floor(real / 60000) + 1) * 60000;
        bool edge = alarm_real <= tick_real;
        real = (edge ? alarm_real : tick_real) + latency_ms;
        long long rtc_s = (long long)floor(real / 1000);
        long long system_s = (long long)floor((real * rate + offset) / 1000);
        if (timekeeping_rtc_resync(system_s, rtc_s, edge)) {
            offset = (double)rtc_s * 1000 - real * rate;
        }
        long long now = (long long)floor((real * rate + offset) / 1000);
        if (shown >= 0 && (now > shown + 1 || now < shown)) {
            bad++;
        }
        shown = now;
    }
    return bad;
}

/* Setting the clock to the RTC's whole second at every tick lost each tick's latency, and the
 * seconds shown skipped one every so often (M3a review). */
static void test_ticks_that_take_time_show_every_second(void)
{
    TEST_ASSERT_EQUAL_INT(0, out_of_order(0.0, 30, 10));
    TEST_ASSERT_EQUAL_INT(0, out_of_order(0.0, 5, 10));
}

/* The RC oscillator that keeps time in light sleep drifts; the minute alarm takes the phase again. */
static void test_a_drifting_clock_shows_every_second(void)
{
    TEST_ASSERT_EQUAL_INT(0, out_of_order(0.004, 30, 10));
    TEST_ASSERT_EQUAL_INT(0, out_of_order(-0.004, 30, 10));
}

static void test_the_rtc_wins_when_the_clocks_disagree(void)
{
    TEST_ASSERT_TRUE(timekeeping_rtc_resync(1000, 1000, true)); /* its second just began */
    TEST_ASSERT_FALSE(timekeeping_rtc_resync(1000, 1000, false));
    TEST_ASSERT_FALSE(timekeeping_rtc_resync(1001, 1000, false)); /* ahead by less than a second or so */
    TEST_ASSERT_TRUE(timekeeping_rtc_resync(1002, 1000, false));
    TEST_ASSERT_TRUE(timekeeping_rtc_resync(999, 1000, false));    /* behind */
    TEST_ASSERT_TRUE(timekeeping_rtc_resync(0, 1790000000, false)); /* boot */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ticks_that_take_time_show_every_second);
    RUN_TEST(test_a_drifting_clock_shows_every_second);
    RUN_TEST(test_the_rtc_wins_when_the_clocks_disagree);
    return UNITY_END();
}
