#define _POSIX_C_SOURCE 200809L /* setenv, tzset */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "energy.h"
#include "unity.h"
#include "util_time.h"

/* The house's energy (spec §11.6, D36): SolaX Cloud's real-time reply, its signs, the battery's
 * rule, today's totals and the quarter-hour readings. solax-documented.json is the example in SolaX's
 * user monitoring API V4.0 (its serial numbers blanked there too); the others are built in its
 * format, as there is no inverter to record them from: a string inverter by day, a hybrid charging
 * its battery, and a refusal. */

#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

static char s_json[4096];
static size_t s_len;
static char s_err[96];

void setUp(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
}

void tearDown(void) {}

static const char *fixture(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    s_len = fread(s_json, 1, sizeof(s_json) - 1, f);
    fclose(f);
    s_json[s_len] = '\0';
    return s_json;
}

static time_t local(int y, int mo, int d, int h, int mi)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
                    .tm_isdst = -1 };
    return mktime(&t);
}

static int32_t day_of(int y, int mo, int d)
{
    return (int32_t)util_days_from_civil(y, mo, d);
}

static energy_reading_t parsed(const char *name)
{
    const char *json = fixture(name);
    energy_reading_t r;
    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, s_len, &r, s_err, sizeof(s_err)), s_err);
    return r;
}

/* --- the request --- */

static void test_the_request_names_the_token_and_the_dongle(void)
{
    char url[ENERGY_URL_MAX];
    TEST_ASSERT_TRUE(energy_solax_url(url, sizeof(url), "20200722185111234567890", "ABCDEFGHIJ") > 0);
    TEST_ASSERT_EQUAL_STRING("https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do"
                             "?tokenId=20200722185111234567890&sn=ABCDEFGHIJ",
                             url);
    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "2020&sn=x", "ABCDEFGHIJ")); /* nothing escapes */
    TEST_ASSERT_EQUAL_STRING("", url);
    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "", "ABCDEFGHIJ"));
    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "2020", ""));
    TEST_ASSERT_EQUAL(0, energy_solax_url(url, 40, "20200722185111234567890", "ABCDEFGHIJ"));
}

/* --- the reply --- */

static void test_the_documented_reply(void)
{
    energy_reading_t r = parsed("solax-documented.json");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)local(2021, 3, 13, 19, 9) + 49, r.at); /* local time */
    TEST_ASSERT_EQUAL_INT32(0, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(0, r.grid_w);
    TEST_ASSERT_EQUAL_INT32(0, r.load_w);
    TEST_ASSERT_EQUAL_UINT32(12600, r.yield_wh);
    TEST_ASSERT_EQUAL_INT16(0, r.soc);
    TEST_ASSERT_EQUAL_UINT8(4, r.inverter); /* X1-Boost/Air/Mini, a string inverter */
}

static void test_a_string_inverter_by_day(void)
{
    energy_reading_t r = parsed("solax-string.json");
    TEST_ASSERT_EQUAL_INT32(3560, r.pv_w);    /* its two strings, before the inverter */
    TEST_ASSERT_EQUAL_INT32(-2560, r.grid_w); /* SolaX's feed-in is positive while exporting; ours imports */
    TEST_ASSERT_EQUAL_INT32(860, r.load_w);   /* what the inverter gives less what goes out */
    TEST_ASSERT_EQUAL_INT32(0, r.bat_w);
    TEST_ASSERT_EQUAL_UINT32(9400, r.yield_wh);
    TEST_ASSERT_EQUAL_UINT32(1523450, r.to_grid_wh);
    TEST_ASSERT_EQUAL_UINT32(2210170, r.from_grid_wh);
    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
}

static void test_without_its_strings_the_inverter_output_counts(void)
{
    const char *json = "{\"success\":true,\"result\":{\"acpower\":1500.0,\"feedinpower\":-300.0,"
                       "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
    energy_reading_t r;
    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT32(1500, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(300, r.grid_w); /* importing */
    TEST_ASSERT_EQUAL_INT32(1800, r.load_w);
    TEST_ASSERT_EQUAL_INT16(-1, r.soc); /* no charge in the reply */
    TEST_ASSERT_EQUAL_UINT8(0, r.inverter);
}

static void test_a_hybrid_shows_its_battery(void)
{
    energy_reading_t r = parsed("solax-hybrid.json");
    TEST_ASSERT_EQUAL_INT32(3420, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(-1360, r.grid_w);
    TEST_ASSERT_EQUAL_INT32(860, r.load_w);
    TEST_ASSERT_EQUAL_INT32(1200, r.bat_w); /* charging */
    TEST_ASSERT_EQUAL_INT16(64, r.soc);
    TEST_ASSERT_EQUAL_UINT8(5, r.inverter); /* X3-Hybrid */
    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_OFF, &r));
}

static void test_the_battery_rule(void)
{
    energy_reading_t r = parsed("solax-string.json");
    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_ON, &r));
    static const uint8_t k_hybrids[] = { 2, 3, 5, 10, 13 }; /* X-Hybrid, X1-, X3-, A1-Hybrid, J1-ESS */
    for (size_t i = 0; i < sizeof(k_hybrids); i++) {
        r.inverter = k_hybrids[i];
        TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
    }
    r.inverter = 7;
    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
    r.soc = 12; /* a charge above 0 % says there is one */
    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, NULL));
}

static void test_eps_loads_count_in_the_house(void)
{
    const char *json = "{\"success\":true,\"result\":{\"acpower\":1000.0,\"feedinpower\":0.0,\"peps1\":120.0,"
                       "\"peps2\":null,\"peps3\":30.0,\"uploadTime\":\"2026-10-05 13:17:02\"}}";
    energy_reading_t r;
    TEST_ASSERT_TRUE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT32(1150, r.load_w);
}

/* Values out of reason make no nonsense of a reading (Review Focus): a negative AC power without DC strings (a hybrid
 * charging from the grid) is no solar; numbers past any range clamp instead of overflowing a cast. */
static void test_values_out_of_reason_are_clamped(void)
{
    const char *json = "{\"success\":true,\"result\":{\"acpower\":-2000.0,\"feedinpower\":-2500.0,"
                       "\"inverterType\":1e300,\"soc\":1e300,\"yieldtoday\":-3,"
                       "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
    energy_reading_t r;
    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT32(0, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(2500, r.grid_w);
    TEST_ASSERT_EQUAL_INT32(500, r.load_w);
    TEST_ASSERT_EQUAL_UINT8(0, r.inverter);
    TEST_ASSERT_EQUAL_INT16(100, r.soc);
    TEST_ASSERT_EQUAL_UINT32(0, r.yield_wh);
    json = "{\"success\":true,\"result\":{\"powerdc1\":1e308,\"powerdc2\":1e308,"
           "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT32(10000000, r.pv_w); /* their sum is past any number: 10 MW */
}

static void test_a_refusal_gives_its_reason(void)
{
    const char *json = fixture("solax-refused.json");
    energy_reading_t r;
    TEST_ASSERT_FALSE(energy_parse_solax(json, s_len, &r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("tokenId is invalid", s_err);
}

static void test_refusals(void)
{
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "not json", "not a JSON object" },
        { "{\"success\":false}", "refused" },
        { "{\"success\":true,\"result\":null}", "no data" },
        { "{\"success\":true,\"result\":{\"acpower\":1.0,\"uploadTime\":\"yesterday\"}}", "no data" },
        { "{\"success\":true,\"result\":{\"uploadTime\":\"2026-10-05 13:17:02\"}}", "no data" },
        { "{\"success\":true,\"result\":{\"a\":[[[1]]]}}", "nested too deeply" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        energy_reading_t r;
        TEST_ASSERT_FALSE(energy_parse_solax(k_cases[i].json, strlen(k_cases[i].json), &r, s_err, sizeof(s_err)));
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

/* --- the day --- */

static energy_reading_t reading(time_t at, int32_t pv_w, double to_kwh, double from_kwh, double yield_kwh)
{
    energy_reading_t r;
    memset(&r, 0, sizeof(r));
    r.at = (uint32_t)at;
    r.pv_w = pv_w;
    r.soc = -1;
    r.to_grid_wh = (uint32_t)(to_kwh * 1000.0 + 0.5);
    r.from_grid_wh = (uint32_t)(from_kwh * 1000.0 + 0.5);
    r.yield_wh = (uint32_t)(yield_kwh * 1000.0 + 0.5);
    return r;
}

static void test_the_reading_nearest_midnight_starts_the_days_totals(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 4, 23, 50), 0, 1000.0, 2000.0, 9.0);
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 0, 5), 0, 1000.1, 2000.2, 0.0); /* 5 min after: nearer than 23:50 */
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 12, 0), 2500, 1005.0, 2001.0, 6.0);
    energy_day_add(&d, &r);
    int32_t today = day_of(2026, 10, 5);
    TEST_ASSERT_EQUAL_INT32(today, d.day);
    TEST_ASSERT_EQUAL_UINT32(4900, energy_to_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_UINT32(800, energy_from_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today + 1)); /* not today's reading */
}

static void test_a_reading_before_midnight_can_be_its_base(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 4, 23, 58), 0, 1000.0, 2000.0, 9.0);
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 0, 40), 0, 1000.5, 2000.5, 0.0);
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 12, 0), 2500, 1005.0, 2001.0, 6.0);
    energy_day_add(&d, &r);
    TEST_ASSERT_EQUAL_UINT32(5000, energy_to_grid_wh(&d, &r, day_of(2026, 10, 5)));
}

static void test_a_day_without_a_reading_near_midnight_has_no_totals(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 4, 22, 30), 0, 1000.0, 2000.0, 9.0); /* 90 min before */
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 5, 30), 0, 1000.0, 2001.0, 0.0); /* the morning's sync */
    energy_day_add(&d, &r);
    int32_t today = day_of(2026, 10, 5);
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_from_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_INT(-1, energy_self_pct(&d, &r, today));
}

static void test_quarter_hours_average_their_readings(void)
{
    energy_day_t d;
    energy_day_init(&d);
    static const struct {
        int mi;
        int32_t w;
    } k_readings[] = { { 1, 2000 }, { 6, 3000 }, { 11, 4004 }, { 16, 1000 } };
    energy_reading_t r;
    for (size_t i = 0; i < sizeof(k_readings) / sizeof(k_readings[0]); i++) {
        r = reading(local(2026, 10, 5, 12, k_readings[i].mi), k_readings[i].w, 1000.0, 2000.0, 5.0);
        energy_day_add(&d, &r);
    }
    energy_day_add(&d, &r); /* the same upload again counts once */
    const uint16_t *q = energy_day_q(&d, day_of(2026, 10, 5));
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_UINT16(300, q[48]); /* 12:00-12:15: 3.0 kW on average, in tens of W */
    TEST_ASSERT_EQUAL_UINT16(100, q[49]);
    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[47]);
    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[50]);
    TEST_ASSERT_NULL(energy_day_q(&d, day_of(2026, 10, 6)));
}

static void test_a_new_day_starts_afresh(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 4, 12, 1), 2000, 1000.0, 2000.0, 5.0);
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 9, 1), 1000, 1000.0, 2000.0, 1.0);
    energy_day_add(&d, &r);
    const uint16_t *q = energy_day_q(&d, day_of(2026, 10, 5));
    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[48]);
    TEST_ASSERT_EQUAL_UINT16(100, q[36]);
    r = reading(local(2026, 10, 4, 18, 0), 3000, 1000.0, 2000.0, 7.0); /* an older reading is left out */
    energy_day_add(&d, &r);
    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[72]);
}

static void test_own_use_is_the_share_the_house_kept(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 5, 0, 5), 0, 1000.0, 2000.0, 0.0);
    energy_day_add(&d, &r);
    r = reading(local(2026, 10, 5, 13, 17), 3000, 1004.9, 2000.6, 9.4);
    energy_day_add(&d, &r);
    int32_t today = day_of(2026, 10, 5);
    TEST_ASSERT_EQUAL_INT(48, energy_self_pct(&d, &r, today)); /* (9.4 - 4.9) / 9.4 */
    r.yield_wh = 0; /* nothing produced yet */
    TEST_ASSERT_EQUAL_INT(-1, energy_self_pct(&d, &r, today));
    r.yield_wh = 4000; /* more went out than the inverter counted: none kept */
    TEST_ASSERT_EQUAL_INT(0, energy_self_pct(&d, &r, today));
}

/* A reading that carries today's totals itself (the Developer API's statistics, D37) needs no reading near midnight:
 * its totals are today's as they are, and of no other day. */
static void test_a_reading_with_todays_totals_needs_no_midnight(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t r = reading(local(2026, 10, 5, 13, 17), 3000, 4.9, 0.6, 9.4);
    r.today = true;
    energy_day_add(&d, &r);
    int32_t today = day_of(2026, 10, 5);
    TEST_ASSERT_EQUAL_UINT32(4900, energy_to_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_UINT32(600, energy_from_grid_wh(&d, &r, today));
    TEST_ASSERT_EQUAL_INT(48, energy_self_pct(&d, &r, today));
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today + 1)); /* tomorrow has none yet */
    r.today = false; /* totals since installation want their midnight */
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today));
}

static void test_a_reading_with_todays_totals_is_no_midnight_base(void)
{
    energy_day_t d;
    energy_day_init(&d);
    energy_reading_t dev = reading(local(2026, 10, 5, 0, 2), 0, 0.0, 0.1, 0.0); /* the Developer API's, today's */
    dev.today = true;
    energy_day_add(&d, &dev);
    TEST_ASSERT_EQUAL_UINT32(0, d.base_at); /* a switch to the Token ID source the same day finds no base */
    energy_reading_t late = reading(local(2026, 10, 5, 23, 55), 0, 4.9, 0.6, 0.0);
    late.today = true;
    energy_day_add(&d, &late);
    TEST_ASSERT_EQUAL_UINT32(0, d.next_at); /* nor tomorrow's */
}

static void test_a_reading_is_fresh_for_15_minutes(void)
{
    energy_reading_t r = reading(local(2026, 10, 5, 13, 0), 0, 0, 0, 0);
    TEST_ASSERT_TRUE(energy_fresh(&r, r.at + ENERGY_FRESH_S));
    TEST_ASSERT_FALSE(energy_fresh(&r, r.at + ENERGY_FRESH_S + 1));
    TEST_ASSERT_TRUE(energy_fresh(&r, r.at - 120)); /* the inverter's clock a little ahead */
    r.at = 0;
    TEST_ASSERT_FALSE(energy_fresh(&r, 1791195300));
}

/* A reading ahead of the clock is fresh only within the same 15 minutes; one more than a day ahead (a dongle's clock or
 * a site's time zone gone wrong) is refused, as it would hold the day's totals until its own day came; an upload time
 * past what the reading keeps is no data (Review Focus). */
static void test_a_reading_from_the_future_is_refused(void)
{
    energy_reading_t r = reading(local(2026, 10, 5, 13, 0), 0, 0, 0, 0);
    TEST_ASSERT_FALSE(energy_fresh(&r, r.at - ENERGY_FRESH_S - 1));
    TEST_ASSERT_FALSE(energy_reading_ahead(&r, r.at - ENERGY_AHEAD_S));
    TEST_ASSERT_TRUE(energy_reading_ahead(&r, r.at - ENERGY_AHEAD_S - 1));
    TEST_ASSERT_FALSE(energy_reading_ahead(&r, 0)); /* no clock: nothing to tell it by */
    const char *json = "{\"success\":true,\"result\":{\"acpower\":100.0,\"uploadTime\":\"9999-12-31 23:59:59\"}}";
    TEST_ASSERT_FALSE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("no data", s_err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_request_names_the_token_and_the_dongle);
    RUN_TEST(test_the_documented_reply);
    RUN_TEST(test_a_string_inverter_by_day);
    RUN_TEST(test_without_its_strings_the_inverter_output_counts);
    RUN_TEST(test_a_hybrid_shows_its_battery);
    RUN_TEST(test_the_battery_rule);
    RUN_TEST(test_eps_loads_count_in_the_house);
    RUN_TEST(test_values_out_of_reason_are_clamped);
    RUN_TEST(test_a_refusal_gives_its_reason);
    RUN_TEST(test_refusals);
    RUN_TEST(test_the_reading_nearest_midnight_starts_the_days_totals);
    RUN_TEST(test_a_reading_before_midnight_can_be_its_base);
    RUN_TEST(test_a_day_without_a_reading_near_midnight_has_no_totals);
    RUN_TEST(test_quarter_hours_average_their_readings);
    RUN_TEST(test_a_new_day_starts_afresh);
    RUN_TEST(test_own_use_is_the_share_the_house_kept);
    RUN_TEST(test_a_reading_with_todays_totals_needs_no_midnight);
    RUN_TEST(test_a_reading_with_todays_totals_is_no_midnight_base);
    RUN_TEST(test_a_reading_is_fresh_for_15_minutes);
    RUN_TEST(test_a_reading_from_the_future_is_refused);
    return UNITY_END();
}
