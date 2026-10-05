#define _POSIX_C_SOURCE 200809L /* setenv, tzset */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "solar.h"
#include "unity.h"
#include "util_time.h"

/* The PV forecast (spec §11.5, D35): our model, the local quarter hours and the replies. The
 * Open-Meteo and Forecast.Solar fixtures were fetched on 2026-10-05 for Brno (49.1951, 16.6068), a
 * plane of 5 kWp tilted 35° facing south (Forecast.Solar's free tier; its rate limit's address is
 * replaced). Forecast.Solar with a key and Solcast are built in their documented formats, as there
 * is no account to record them with. */

#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

static char s_json[32768];
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

/* A local time in Prague; `dst` 1 or 0 picks the summer or winter one of an hour that comes twice. */
static time_t local_at(int y, int mo, int d, int h, int mi, int dst)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
                    .tm_isdst = dst };
    return mktime(&t);
}

static time_t local(int y, int mo, int d, int h, int mi)
{
    return local_at(y, mo, d, h, mi, -1);
}

static int32_t day_of(int y, int mo, int d)
{
    return (int32_t)util_days_from_civil(y, mo, d);
}

static const solar_plane_t k_south = { .kwp = 5.0f, .tilt = 35, .azimuth = 0 };

/* --- the model --- */

static void test_the_model_follows_the_formula(void)
{
    /* 5 kWp, 800 W/m², 20 °C, 14 % losses: the cell at 44.8 °C, 0.9208 for the heat, 3167.6 W */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3167.6f, solar_model_w(5.0f, 800.0f, 20.0f, 14));
}

static void test_the_model_gains_in_the_cold(void)
{
    /* 1000 W/m² at -10 °C: the cell at 21 °C gives 1.6 % more than the rating, less the losses */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 4368.8f, solar_model_w(5.0f, 1000.0f, -10.0f, 14));
}

static void test_no_light_no_power(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, 0.0f, 30.0f, 14));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, -3.0f, 30.0f, 14)); /* a negative irradiance is none */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, 1000.0f, 300.0f, 0)); /* never below nothing */
}

/* --- the local quarter hours --- */

static void test_a_quarter_hour_period_fills_its_own(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2000.0f); /* 12:00-12:15: quarter 48 */
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 1234, &f);
    TEST_ASSERT_EQUAL_INT32(day_of(2026, 10, 5), f.day);
    TEST_ASSERT_EQUAL_UINT32(1234, f.fetched);
    TEST_ASSERT_EQUAL_UINT16(200, f.q[0][48]);
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][47]);
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][49]);
    TEST_ASSERT_EQUAL_UINT32(500, f.wh[0]); /* 2 kW for a quarter hour */
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]); /* nothing came for tomorrow */
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[2]);
}

static void test_a_half_hour_fills_two_quarter_hours(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 12, 30), 1800, 3000.0f); /* Solcast's 12:00-12:30 */
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][48]);
    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][49]);
    TEST_ASSERT_EQUAL_UINT32(1500, f.wh[0]);
}

static void test_a_line_is_integrated_by_quarter_hour(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_line(&acc, local(2026, 10, 5, 12, 0), 0.0f, local(2026, 10, 5, 13, 0), 4000.0f); /* Forecast.Solar */
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(50, f.q[0][48]); /* the mean of each quarter hour of the line */
    TEST_ASSERT_EQUAL_UINT16(150, f.q[0][49]);
    TEST_ASSERT_EQUAL_UINT16(250, f.q[0][50]);
    TEST_ASSERT_EQUAL_UINT16(350, f.q[0][51]);
    TEST_ASSERT_EQUAL_UINT32(2000, f.wh[0]);
}

static void test_periods_beyond_the_three_days_are_left_out(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 0, 0), 900, 1000.0f); /* yesterday's 23:45-24:00 */
    solar_acc_period(&acc, local(2026, 10, 7, 12, 15), 900, 1000.0f); /* the day after tomorrow */
    solar_acc_period(&acc, local(2026, 10, 8, 12, 15), 900, 1000.0f); /* a fourth day */
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[0]);
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
    TEST_ASSERT_EQUAL_UINT32(250, f.wh[2]);
}

static void test_the_night_the_clocks_go_back_keeps_both_hours(void)
{
    /* 2026-10-25: 02:00-03:00 comes twice, in summer time and then in winter time; both land on
     * quarters 8-11, so the day keeps its energy */
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 25, 1, 0));
    time_t summer = local_at(2026, 10, 25, 2, 15, 1);
    solar_acc_period(&acc, summer, 900, 400.0f);
    solar_acc_period(&acc, summer + 3600, 900, 400.0f); /* 02:00-02:15 again, in winter time */
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(80, f.q[0][8]);
    TEST_ASSERT_EQUAL_UINT32(200, f.wh[0]);
}

static void test_the_night_the_clocks_go_forward_skips_an_hour(void)
{
    /* 2026-03-29: 02:00 becomes 03:00; the hour after 01:00 UTC is 03:00-04:00 local */
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 3, 29, 1, 0));
    time_t t = local(2026, 3, 29, 1, 45);
    for (int i = 1; i <= 8; i++) {
        solar_acc_period(&acc, t + i * 900, 900, 400.0f); /* 01:45-03:45 UTC+1 ... */
    }
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][7]);  /* 01:45 */
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][8]);   /* 02:00 never came */
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][11]);
    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][12]); /* 03:00 */
    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][18]);
    TEST_ASSERT_EQUAL_UINT32(800, f.wh[0]);
}

static void test_the_inverter_caps_each_quarter_hour(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2500.0f); /* two planes */
    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2500.0f);
    solar_acc_period(&acc, local(2026, 10, 5, 12, 30), 900, 1000.0f);
    solar_acc_cap(&acc, 4000.0f);
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(400, f.q[0][48]);
    TEST_ASSERT_EQUAL_UINT16(100, f.q[0][49]);
}

/* A provider's times out of all reason count only within the three days, and fast: a line or a period that reaches
 * years away is cut to them before it is integrated (Review Focus). */
static void test_times_far_off_count_only_within_the_three_days(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    clock_t c0 = clock();
    solar_acc_line(&acc, 0, 1000.0f, local(2026, 10, 6, 0, 0), 1000.0f); /* from 1970 */
    TEST_ASSERT_TRUE_MESSAGE(clock() - c0 < CLOCKS_PER_SEC / 20, "a line from 1970");
    solar_acc_line(&acc, local(2026, 10, 7, 0, 0), 400.0f, (time_t)253402300799, 400.0f); /* to the year 9999 */
    solar_acc_period(&acc, local(2026, 10, 6, 0, 0), INT32_MAX, 1000.0f);                /* from 1958 */
    TEST_ASSERT_TRUE_MESSAGE(clock() - c0 < CLOCKS_PER_SEC / 20, "a line to 9999");
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT32(48000, f.wh[0]); /* the line's and the period's 24 h of 1 kW */
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
    TEST_ASSERT_EQUAL_UINT32(9600, f.wh[2]);
}

/* Power below zero, not a number, or beyond what a quarter hour keeps (655 kW) stays within those bounds, so no
 * day's total wraps (Review Focus). */
static void test_power_out_of_reason_stays_within_bounds(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
    solar_acc_line(&acc, local(2026, 10, 5, 12, 0), -100.0f, local(2026, 10, 5, 12, 15), -100.0f);
    solar_acc_period(&acc, local(2026, 10, 6, 12, 15), 900, 1e30f);
    solar_acc_period(&acc, local(2026, 10, 6, 12, 30), 900, NAN);
    solar_acc_period(&acc, local(2026, 10, 7, 12, 15), 900, -5000.0f);
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT32(0, f.wh[0]);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, f.q[1][48]);
    TEST_ASSERT_EQUAL_UINT16(0, f.q[1][49]);
    TEST_ASSERT_EQUAL_UINT32(163838, f.wh[1]); /* 655.35 kW for a quarter hour */
    TEST_ASSERT_EQUAL_UINT32(0, f.wh[2]);
}

static void test_quarter_hours_a_reply_leaves_out_keep_the_last_forecast(void)
{
    /* Solcast sends nothing before now: the morning keeps what the last forecast said */
    solar_forecast_t old;
    memset(&old, 0, sizeof(old));
    old.day = day_of(2026, 10, 5);
    old.q[0][40] = 150;
    old.q[0][48] = 200;
    old.wh[0] = 875;
    old.wh[1] = SOLAR_WH_NONE;
    old.wh[2] = SOLAR_WH_NONE;
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 3000.0f);
    solar_forecast_t f;
    solar_acc_finish(&acc, &old, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(150, f.q[0][40]); /* kept */
    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][48]); /* replaced */
    TEST_ASSERT_EQUAL_UINT32(1125, f.wh[0]);
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
}

static void test_yesterdays_forecast_gives_today_its_tomorrow(void)
{
    solar_forecast_t old;
    memset(&old, 0, sizeof(old));
    old.day = day_of(2026, 10, 4);
    old.q[1][40] = 120; /* 5 October, 10:00 */
    old.wh[0] = 0;
    old.wh[1] = 300;
    old.wh[2] = 9000;
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 0));
    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 3000.0f);
    solar_forecast_t f;
    solar_acc_finish(&acc, &old, 0, &f); /* finishing into the old one is allowed */
    TEST_ASSERT_EQUAL_UINT16(120, f.q[0][40]);
    TEST_ASSERT_EQUAL_UINT32(1050, f.wh[0]);
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]); /* old's day after is no quarter hours to keep */
    solar_acc_finish(&acc, &old, 0, &old);
    TEST_ASSERT_EQUAL_UINT16(120, old.q[0][40]);
}

/* --- what the fields read --- */

static solar_forecast_t two_quarters(void)
{
    solar_forecast_t f;
    memset(&f, 0, sizeof(f));
    f.day = day_of(2026, 10, 5);
    f.q[0][48] = 200; /* 2 kW at 12:00 */
    f.q[0][49] = 400; /* 4 kW at 12:15 */
    f.q[1][50] = 100;
    f.wh[0] = 1500;
    f.wh[1] = 250;
    f.wh[2] = 9000;
    return f;
}

static void test_today_and_tomorrow_follow_the_local_day(void)
{
    solar_forecast_t f = two_quarters();
    int32_t d = f.day;
    TEST_ASSERT_EQUAL_PTR(f.q[0], solar_day(&f, d));
    TEST_ASSERT_EQUAL_PTR(f.q[1], solar_day(&f, d + 1)); /* after midnight, tomorrow is today */
    TEST_ASSERT_NULL(solar_day(&f, d + 2));
    TEST_ASSERT_NULL(solar_day(&f, d - 1));
    TEST_ASSERT_EQUAL_UINT32(9000, solar_day_wh(&f, d + 2));
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(&f, d + 3));
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(&f, d - 1));
    f.wh[1] = SOLAR_WH_NONE;
    TEST_ASSERT_NULL(solar_day(&f, d + 1)); /* no data for that day */
    solar_forecast_t none;
    memset(&none, 0, sizeof(none));
    TEST_ASSERT_NULL(solar_day(&none, d));
    TEST_ASSERT_NULL(solar_day(NULL, d));
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(NULL, d));
}

static void test_what_is_left_counts_the_rest_of_the_day(void)
{
    solar_forecast_t f = two_quarters();
    TEST_ASSERT_EQUAL_UINT32(1500, solar_left_wh(&f, f.day, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1250, solar_left_wh(&f, f.day, 48, 450)); /* half of 12:00's, all of 12:15's */
    TEST_ASSERT_EQUAL_UINT32(0, solar_left_wh(&f, f.day, 50, 0));
    TEST_ASSERT_EQUAL_UINT32(250, solar_left_wh(&f, f.day + 1, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_left_wh(&f, f.day + 2, 0, 0));
}

static void test_the_peak_is_the_highest_quarter_hour(void)
{
    solar_forecast_t f = two_quarters();
    uint32_t w;
    int quarter;
    TEST_ASSERT_TRUE(solar_peak(&f, f.day, &w, &quarter));
    TEST_ASSERT_EQUAL_UINT32(4000, w);
    TEST_ASSERT_EQUAL_INT(49, quarter);
    memset(f.q[0], 0, sizeof(f.q[0]));
    TEST_ASSERT_FALSE(solar_peak(&f, f.day, &w, &quarter)); /* no output: no peak */
    TEST_ASSERT_FALSE(solar_peak(&f, f.day + 2, &w, &quarter));
}

/* --- Open-Meteo --- */

static void test_open_meteo_asks_for_one_plane(void)
{
    char url[SOLAR_URL_MAX];
    TEST_ASSERT_TRUE(solar_open_meteo_url(url, sizeof(url), 491951, 166068, &k_south) > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.open-meteo.com/v1/forecast?latitude=49.1951&longitude=16.6068"
                             "&minutely_15=global_tilted_irradiance,temperature_2m&tilt=35&azimuth=0"
                             "&timezone=auto&timeformat=unixtime&forecast_days=3",
                             url);
    const solar_plane_t east = { .kwp = 2.4f, .tilt = 20, .azimuth = -90 };
    TEST_ASSERT_TRUE(solar_open_meteo_url(url, sizeof(url), -338688, -1512093, &east) > 0);
    TEST_ASSERT_NOT_NULL(strstr(url, "latitude=-33.8688&longitude=-151.2093"));
    TEST_ASSERT_NOT_NULL(strstr(url, "&tilt=20&azimuth=-90&"));
    TEST_ASSERT_EQUAL(0, solar_open_meteo_url(url, 40, 491951, 166068, &k_south)); /* never half a URL */
    TEST_ASSERT_EQUAL_STRING("", url);
}

static solar_forecast_t open_meteo_brno(int planes)
{
    const char *json = fixture("open-meteo-brno.json");
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    for (int i = 0; i < planes; i++) {
        TEST_ASSERT_TRUE_MESSAGE(solar_parse_open_meteo(json, s_len, &k_south, 14, &acc, s_err, sizeof(s_err)),
                                 s_err);
    }
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    return f;
}

static void test_open_meteo_gives_three_days(void)
{
    solar_forecast_t f = open_meteo_brno(1);
    TEST_ASSERT_EQUAL_INT32(day_of(2026, 10, 5), f.day);
    TEST_ASSERT_UINT32_WITHIN(30, 16644, f.wh[0]); /* the model over the reply, worked out by hand */
    TEST_ASSERT_UINT32_WITHIN(30, 22216, f.wh[1]);
    TEST_ASSERT_UINT32_WITHIN(30, 18652, f.wh[2]);
    uint32_t w;
    int quarter;
    TEST_ASSERT_TRUE(solar_peak(&f, f.day, &w, &quarter));
    TEST_ASSERT_EQUAL_INT(49, quarter); /* 12:15-12:30 */
    TEST_ASSERT_UINT32_WITHIN(10, 2870, w);
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][0]); /* night */
}

static void test_two_planes_add_up(void)
{
    solar_forecast_t f = open_meteo_brno(2);
    TEST_ASSERT_UINT32_WITHIN(60, 2 * 16644, f.wh[0]);
}

static void test_open_meteo_leaves_out_what_it_has_no_value_for(void)
{
    const char *json = "{\"minutely_15\":{\"time\":[1791195300,1791196200],"
                       "\"global_tilted_irradiance\":[null,500.0],\"temperature_2m\":[12.0,null]}}";
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    TEST_ASSERT_FALSE(solar_parse_open_meteo(json, strlen(json), &k_south, 14, &acc, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("no data", s_err);
}

static void test_open_meteo_refusals(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "not json", "not a JSON object" },
        { "{}", "no data" },
        { "{\"minutely_15\":{\"time\":[1791195300],\"global_tilted_irradiance\":[],\"temperature_2m\":[1]}}",
          "no data" },
        { "{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90°.\"}", "Latitude must be in ran" },
        { "{\"error\":true,\"reason\":\"Latitude out of range:°\"}", "Latitude out of range:" }, /* never half a ° */
        { "{\"minutely_15\":{\"time\":[[[[1]]]]}}", "nested too deeply" },
        { "{\"minutely_15\":{\"time\":[1e300],\"global_tilted_irradiance\":[500],\"temperature_2m\":[10]}}",
          "no data" }, /* a time no clock has: left out, never cast */
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        TEST_ASSERT_FALSE(solar_parse_open_meteo(k_cases[i].json, strlen(k_cases[i].json), &k_south, 14, &acc,
                                                 s_err, 24)); /* the sync's detail */
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

/* --- Forecast.Solar --- */

static void test_forecast_solar_asks_without_a_key(void)
{
    char url[SOLAR_URL_MAX];
    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, &k_south, 1, "") > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/estimate/watts/49.1951/16.6068/35/0/5?time=utc", url);
}

static void test_forecast_solar_with_a_key_names_both_planes(void)
{
    const solar_plane_t planes[2] = { { .kwp = 5.2f, .tilt = 35, .azimuth = 0 },
                                      { .kwp = 2.45f, .tilt = 20, .azimuth = -90 } };
    char url[SOLAR_URL_MAX];
    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 2, "AbC123xyz") > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/AbC123xyz/estimate/watts/49.1951/16.6068/35/0/5.2"
                             "/20/-90/2.45?time=utc",
                             url);
    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 2, "") > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/estimate/watts/49.1951/16.6068/35/0/5.2?time=utc",
                             url); /* without a key, the first plane alone: a free account takes one */
    TEST_ASSERT_EQUAL(0, solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 1, "ab/c"));
    TEST_ASSERT_EQUAL_STRING("", url); /* a key goes into the path as it is */
}

static void test_forecast_solar_follows_the_line_between_its_points(void)
{
    const char *json = fixture("forecast-solar-brno.json");
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
    TEST_ASSERT_TRUE_MESSAGE(solar_parse_forecast_solar(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_UINT32_WITHIN(30, 15025, f.wh[0]); /* its own watt_hours_day for the same request */
    TEST_ASSERT_UINT32_WITHIN(30, 17343, f.wh[1]);
    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[2]); /* the free tier has two days */
    TEST_ASSERT_EQUAL_UINT16(14, f.q[0][28]); /* 07:00-07:15: from 110 W at 07:00 towards 352 W at 08:00 */
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][20]);  /* before sunrise */
}

static void test_forecast_solar_with_a_key_has_a_third_day(void)
{
    const char *json = fixture("forecast-solar-key.json"); /* 15-minute points for three days */
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
    TEST_ASSERT_TRUE_MESSAGE(solar_parse_forecast_solar(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_UINT32_WITHIN(30, 24703, f.wh[0]);
    TEST_ASSERT_UINT32_WITHIN(30, 26622, f.wh[1]);
    TEST_ASSERT_UINT32_WITHIN(30, 14260, f.wh[2]);
}

static void test_forecast_solar_refusals(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "not json", "not a JSON object" },
        { "{\"result\":null,\"message\":{\"code\":429,\"type\":\"error\",\"text\":\"Rate limit for API calls "
          "reached.\"}}",
          "Rate limit for API call" },
        { "{\"result\":{},\"message\":{\"code\":0,\"type\":\"success\"}}", "no data" },
        { "{\"result\":{\"2026-10-05T05:00:00+00:00\":110}}", "no data" }, /* a point is no line */
        { "{\"result\":{\"yesterday\":1,\"today\":2}}", "no data" },
        { "{\"result\":{\"a\":[[[[1]]]]}}", "nested too deeply" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        TEST_ASSERT_FALSE(solar_parse_forecast_solar(k_cases[i].json, strlen(k_cases[i].json), &acc, s_err, 24));
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

/* --- Solcast --- */

static void test_solcast_asks_for_a_site(void)
{
    char url[SOLAR_URL_MAX];
    TEST_ASSERT_TRUE(solar_solcast_url(url, sizeof(url), "ab12-cd34-ef56-7890") > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.solcast.com.au/rooftop_sites/ab12-cd34-ef56-7890/forecasts"
                             "?format=json&hours=72",
                             url);
    TEST_ASSERT_EQUAL(0, solar_solcast_url(url, sizeof(url), "ab12/../x"));
    TEST_ASSERT_EQUAL(0, solar_solcast_url(url, sizeof(url), ""));
}

static void test_solcast_fills_half_hours_from_now(void)
{
    const char *json = fixture("solcast-brno.json"); /* 72 h from 11:00 */
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    TEST_ASSERT_TRUE_MESSAGE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(359, f.q[0][44]); /* 3.5904 kW from 11:00 to 11:30 */
    TEST_ASSERT_EQUAL_UINT16(359, f.q[0][45]);
    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][43]); /* the morning is past: not in the reply */
    TEST_ASSERT_UINT32_WITHIN(30, 20040, f.wh[0]);
    TEST_ASSERT_UINT32_WITHIN(30, 29295, f.wh[1]);
    TEST_ASSERT_UINT32_WITHIN(30, 15689, f.wh[2]);
}

static void test_two_solcast_sites_add_up(void)
{
    const char *json = fixture("solcast-brno.json");
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    TEST_ASSERT_TRUE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)));
    solar_forecast_t f;
    solar_acc_finish(&acc, NULL, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(718, f.q[0][44]);
}

static void test_solcast_refusals(void)
{
    solar_acc_t acc;
    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "not json", "not a JSON object" },
        { "{\"response_status\":{\"error_code\":\"TooManyRequests\",\"message\":\"You have exceeded your free "
          "daily limit.\",\"errors\":[]}}",
          "You have exceeded your " },
        { "{\"forecasts\":[]}", "no data" },
        { "{\"forecasts\":[{\"pv_estimate\":1.0,\"period_end\":\"soon\",\"period\":\"PT30M\"},"
          "{\"pv_estimate\":1.0,\"period_end\":\"2026-10-05T10:00:00.0000000Z\",\"period\":\"P1D\"}]}",
          "no data" },
        { "{\"forecasts\":[[[[1]]]]}", "nested too deeply" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        TEST_ASSERT_FALSE(solar_parse_solcast(k_cases[i].json, strlen(k_cases[i].json), &acc, s_err, 24));
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

static void test_solcast_keeps_within_its_ten_calls_a_day(void)
{
    const uint32_t at = 1791195300;
    TEST_ASSERT_TRUE(solar_solcast_due(0, 1, at)); /* never asked */
    TEST_ASSERT_FALSE(solar_solcast_due(at, 1, at + 2 * 3600 + 3599));
    TEST_ASSERT_TRUE(solar_solcast_due(at, 1, at + 3 * 3600)); /* 8 a day with one site */
    TEST_ASSERT_FALSE(solar_solcast_due(at, 2, at + 5 * 3600));
    TEST_ASSERT_TRUE(solar_solcast_due(at, 2, at + 6 * 3600)); /* 4 a day, two calls each */
    TEST_ASSERT_TRUE(solar_solcast_due(at, 1, at - 60));       /* the clock went back: don't wait for it */
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, solar_solcast_wait_s(1));
    TEST_ASSERT_EQUAL_UINT32(6 * 3600, solar_solcast_wait_s(2));
}

/* pv.* are fresh as the weather is (spec §5.1), Solcast's wait in place of a shorter sync interval. */
static void test_the_forecast_is_fresh_for_the_sync_interval_and_two_hours(void)
{
    TEST_ASSERT_EQUAL_UINT32(26 * 3600, solar_fresh_s(SOLAR_OPEN_METEO, 1, 24 * 3600)); /* one sync a day */
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, solar_fresh_s(SOLAR_FORECAST_SOLAR, 1, 3600));
    TEST_ASSERT_EQUAL_UINT32(5 * 3600, solar_fresh_s(SOLAR_SOLCAST, 1, 3600)); /* it waits 3 h between calls */
    TEST_ASSERT_EQUAL_UINT32(8 * 3600, solar_fresh_s(SOLAR_SOLCAST, 2, 3600)); /* and 6 h with two sites */
    TEST_ASSERT_EQUAL_UINT32(26 * 3600, solar_fresh_s(SOLAR_SOLCAST, 2, 24 * 3600));
    TEST_ASSERT_EQUAL_UINT32(0, solar_fresh_s(SOLAR_SOLCAST, 1, 0)); /* sync mode manual: never stale */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_model_follows_the_formula);
    RUN_TEST(test_the_model_gains_in_the_cold);
    RUN_TEST(test_no_light_no_power);
    RUN_TEST(test_a_quarter_hour_period_fills_its_own);
    RUN_TEST(test_a_half_hour_fills_two_quarter_hours);
    RUN_TEST(test_a_line_is_integrated_by_quarter_hour);
    RUN_TEST(test_periods_beyond_the_three_days_are_left_out);
    RUN_TEST(test_the_night_the_clocks_go_back_keeps_both_hours);
    RUN_TEST(test_the_night_the_clocks_go_forward_skips_an_hour);
    RUN_TEST(test_the_inverter_caps_each_quarter_hour);
    RUN_TEST(test_times_far_off_count_only_within_the_three_days);
    RUN_TEST(test_power_out_of_reason_stays_within_bounds);
    RUN_TEST(test_quarter_hours_a_reply_leaves_out_keep_the_last_forecast);
    RUN_TEST(test_yesterdays_forecast_gives_today_its_tomorrow);
    RUN_TEST(test_today_and_tomorrow_follow_the_local_day);
    RUN_TEST(test_what_is_left_counts_the_rest_of_the_day);
    RUN_TEST(test_the_peak_is_the_highest_quarter_hour);
    RUN_TEST(test_open_meteo_asks_for_one_plane);
    RUN_TEST(test_open_meteo_gives_three_days);
    RUN_TEST(test_two_planes_add_up);
    RUN_TEST(test_open_meteo_leaves_out_what_it_has_no_value_for);
    RUN_TEST(test_open_meteo_refusals);
    RUN_TEST(test_forecast_solar_asks_without_a_key);
    RUN_TEST(test_forecast_solar_with_a_key_names_both_planes);
    RUN_TEST(test_forecast_solar_follows_the_line_between_its_points);
    RUN_TEST(test_forecast_solar_with_a_key_has_a_third_day);
    RUN_TEST(test_forecast_solar_refusals);
    RUN_TEST(test_solcast_asks_for_a_site);
    RUN_TEST(test_solcast_fills_half_hours_from_now);
    RUN_TEST(test_two_solcast_sites_add_up);
    RUN_TEST(test_solcast_refusals);
    RUN_TEST(test_solcast_keeps_within_its_ten_calls_a_day);
    RUN_TEST(test_the_forecast_is_fresh_for_the_sync_interval_and_two_hours);
    return UNITY_END();
}
