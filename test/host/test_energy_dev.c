#define _POSIX_C_SOURCE 200809L /* setenv, tzset */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "energy_dev.h"
#include "unity.h"

/* SolaX Cloud's Developer API (D37). No account was at hand: the replies are built in the formats a working Home
 * Assistant integration reads (Solax-Developer-API-for-Home-assistant), and the board checks the real ones
 * (`energy raw`). */

#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

static char s_err[64];

void setUp(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
}

void tearDown(void) {}

static energy_dev_site_t site(void)
{
    energy_dev_site_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.plant_id, sizeof(s.plant_id), "1781234567890123456");
    s.business = 1;
    snprintf(s.sn[0], sizeof(s.sn[0]), "H34A10I1234567");
    snprintf(s.sn[1], sizeof(s.sn[1]), "TP0123456789");
    snprintf(s.sn[2], sizeof(s.sn[2]), "MTR0012345");
    return s;
}

static time_t local(int y, int mo, int d, int h, int mi)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
    return mktime(&t);
}

/* --- the token --- */

static void test_the_url_names_the_region(void)
{
    char url[ENERGY_DEV_URL_MAX];
    TEST_ASSERT_TRUE(energy_dev_url(url, sizeof(url), ENERGY_DEV_EU, ENERGY_DEV_TOKEN_PATH) > 0);
    TEST_ASSERT_EQUAL_STRING("https://openapi-eu.solaxcloud.com/openapi/auth/oauth/token", url);
    energy_dev_url(url, sizeof(url), ENERGY_DEV_CN, "/x");
    TEST_ASSERT_EQUAL_STRING("https://openapi-cn.solaxcloud.com/x", url);
    energy_dev_url(url, sizeof(url), ENERGY_DEV_IN, "/x");
    TEST_ASSERT_EQUAL_STRING("https://openapi-in.solaxcloud.com/x", url);
    TEST_ASSERT_EQUAL_UINT(0, energy_dev_url(url, sizeof(url), (energy_dev_region_t)7, "/x"));
    TEST_ASSERT_EQUAL_STRING("", url);
}

static void test_the_token_request_carries_the_credentials(void)
{
    char body[ENERGY_DEV_BODY_MAX];
    TEST_ASSERT_TRUE(energy_dev_token_body(body, sizeof(body), "c5257b5a04074131", "Sec-r3t_X") > 0);
    TEST_ASSERT_EQUAL_STRING("client_id=c5257b5a04074131&client_secret=Sec-r3t_X&grant_type=client_credentials",
                             body);
    TEST_ASSERT_EQUAL_UINT(0, energy_dev_token_body(body, sizeof(body), "c5257b5a", "a&grant_type=password"));
    TEST_ASSERT_EQUAL_UINT(0, energy_dev_token_body(body, sizeof(body), "", "secret"));
}

static void test_a_token_reply_gives_the_token_and_its_life(void)
{
    char token[ENERGY_DEV_TOKEN_MAX];
    uint32_t life = 0;
    const char *json = "{\"code\":0,\"message\":\"success\",\"result\":{\"access_token\":\"eyJhbGciOi.J9-_x~+/=\","
                       "\"token_type\":\"bearer\",\"expires_in\":2591999,\"scope\":\"API_Telemetry_V2\"}}";
    TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_token(json, strlen(json), token, sizeof(token), &life, s_err,
                                                    sizeof(s_err)),
                             s_err);
    TEST_ASSERT_EQUAL_STRING("eyJhbGciOi.J9-_x~+/=", token);
    TEST_ASSERT_EQUAL_UINT32(2591999, life);
    json = "{\"code\":0,\"result\":{\"access_token\":\"abc\"}}";
    TEST_ASSERT_TRUE(energy_dev_parse_token(json, strlen(json), token, sizeof(token), &life, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT32(ENERGY_DEV_TOKEN_S, life);
}

/* A token goes into a request's header as it is: one with a space, a line break or a quote is refused, as is one
 * past the buffer. */
static void test_token_refusals(void)
{
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "not json", "not a JSON object" },
        { "{\"code\":1002,\"message\":\"client_secret is wrong\"}", "client_secret is wrong" },
        { "{\"code\":0,\"result\":{}}", "no token" },
        { "{\"code\":0,\"result\":{\"access_token\":\"ab cd\"}}", "a token of other characters" },
        { "{\"code\":0,\"result\":{\"access_token\":\"ab\\r\\nX-Evil: 1\"}}", "a token of other characters" },
        { "{\"code\":0,\"result\":{\"access_token\":\"0123456789\"}}", "a token too long" }, /* in 8 bytes */
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        char token[8];
        uint32_t life;
        TEST_ASSERT_FALSE_MESSAGE(energy_dev_parse_token(k_cases[i].json, strlen(k_cases[i].json), token,
                                                         sizeof(token), &life, s_err, sizeof(s_err)),
                                  k_cases[i].json);
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

static void test_a_token_is_renewed_ten_minutes_before_it_ends(void)
{
    TEST_ASSERT_TRUE(energy_dev_token_fresh(1791200000 + ENERGY_DEV_RENEW_S + 1, 1791200000));
    TEST_ASSERT_FALSE(energy_dev_token_fresh(1791200000 + ENERGY_DEV_RENEW_S, 1791200000));
    TEST_ASSERT_FALSE(energy_dev_token_fresh(0, 1791200000));
    TEST_ASSERT_FALSE(energy_dev_token_fresh(1791200000 + 3600, 0)); /* no clock: a new one */
}

/* --- the plant and its devices --- */

static void test_the_requests_name_the_plant_and_the_device(void)
{
    energy_dev_site_t s = site();
    char path[ENERGY_DEV_URL_MAX];
    TEST_ASSERT_TRUE(energy_dev_plants_path(path, sizeof(path), 1) > 0);
    TEST_ASSERT_EQUAL_STRING("/openapi/v2/plant/page_plant_info?businessType=1&pageNo=1", path);
    TEST_ASSERT_TRUE(energy_dev_devices_path(path, sizeof(path), &s, ENERGY_DEV_INVERTER) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "/openapi/v2/device/page_device_info?businessType=1&deviceType=1&pageNo=1&plantId=1781234567890123456",
        path);
    TEST_ASSERT_TRUE(energy_dev_realtime_path(path, sizeof(path), &s, ENERGY_DEV_METER) > 0);
    TEST_ASSERT_EQUAL_STRING("/openapi/v2/device/realtime_data?snList=MTR0012345&deviceType=3&businessType=1", path);
    char body[ENERGY_DEV_BODY_MAX];
    TEST_ASSERT_TRUE(energy_dev_stats_body(body, sizeof(body), &s, 2026, 10) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"plantId\":\"1781234567890123456\",\"dateType\":2,\"date\":\"2026-10\","
                             "\"businessType\":1}",
                             body);
    snprintf(s.sn[2], sizeof(s.sn[2]), "MTR&x=1");
    TEST_ASSERT_EQUAL_UINT(0, energy_dev_realtime_path(path, sizeof(path), &s, ENERGY_DEV_METER));
}

static void test_the_first_plant_and_its_devices(void)
{
    energy_dev_site_t s;
    memset(&s, 0, sizeof(s));
    const char *json = "{\"code\":10000,\"message\":\"success\",\"result\":{\"records\":[{\"plantId\":"
                       "\"1781234567890123456\",\"plantName\":\"Home\",\"businessType\":1}],\"total\":1}}";
    TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_plant(json, strlen(json), &s, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("1781234567890123456", s.plant_id);
    TEST_ASSERT_EQUAL_UINT8(1, s.business);
    json = "{\"code\":10000,\"result\":{\"records\":[{\"plantId\":1781234567890123457}]}}"; /* a number: its digits */
    TEST_ASSERT_TRUE(energy_dev_parse_plant(json, strlen(json), &s, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("1781234567890123457", s.plant_id);
    json = "{\"code\":10000,\"result\":{\"records\":[]}}";
    TEST_ASSERT_TRUE(energy_dev_parse_plant(json, strlen(json), &s, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("", s.plant_id); /* none of this business type */
    json = "{\"code\":10000,\"result\":{\"records\":[{\"deviceSn\":\"H34A10I1234567\",\"deviceType\":1,"
           "\"registerNo\":\"SXA1B2C3D4\"}]}}";
    TEST_ASSERT_TRUE(energy_dev_parse_device(json, strlen(json), ENERGY_DEV_INVERTER, &s, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("H34A10I1234567", s.sn[0]);
    json = "{\"code\":10000,\"result\":{\"records\":[]}}";
    TEST_ASSERT_TRUE(energy_dev_parse_device(json, strlen(json), ENERGY_DEV_BATTERY, &s, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("", s.sn[1]); /* no battery */
}

/* --- the real-time values --- */

static void test_the_inverter_gives_the_panels_its_output_and_the_grid(void)
{
    energy_dev_now_t now;
    memset(&now, 0, sizeof(now));
    const char *json = "{\"code\":10000,\"result\":[{\"deviceSn\":\"H34A10I1234567\",\"deviceType\":1,"
                       "\"businessType\":1,\"onlineStatus\":1,\"dataTime\":\"2026-10-05T13:17:02+02:00\","
                       "\"totalActivePower\":3150,\"gridPower\":2290,\"MPPTTotalInputPower\":3420,"
                       "\"mpptMap\":{\"mppt1Power\":1800,\"mppt2Power\":1620},\"dailyYield\":18.4}]}";
    TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                                       sizeof(s_err)),
                             s_err);
    TEST_ASSERT_TRUE(now.have_pv && now.have_ac && now.have_grid && now.have_yield);
    TEST_ASSERT_FALSE(now.have_meter);
    TEST_ASSERT_EQUAL_DOUBLE(3420, now.pv_w);
    TEST_ASSERT_EQUAL_DOUBLE(3150, now.ac_w);
    TEST_ASSERT_EQUAL_DOUBLE(2290, now.feed_w);
    TEST_ASSERT_EQUAL_DOUBLE(18.4, now.yield_kwh);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)local(2026, 10, 5, 13, 17) + 2, now.at);
}

static void test_without_a_total_the_mppts_add_up_and_a_commercial_plant_is_in_kw(void)
{
    energy_dev_now_t now;
    memset(&now, 0, sizeof(now));
    const char *json = "{\"code\":10000,\"result\":[{\"deviceType\":1,\"totalActivePower\":\"3.15\","
                       "\"mpptMap\":{\"mppt1Power\":1.8,\"mppt1Voltage\":380,\"mppt2Power\":\"1.62\"}}]}";
    TEST_ASSERT_TRUE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 4, &now, s_err,
                                               sizeof(s_err)));
    TEST_ASSERT_EQUAL_DOUBLE(3420, now.pv_w); /* the voltage left out */
    TEST_ASSERT_EQUAL_DOUBLE(3150, now.ac_w);
    TEST_ASSERT_EQUAL_UINT32(0, now.at); /* no dataTime */
}

/* The meter at the grid's connection knows the grid best: its power stands whichever device comes first. */
static void test_the_meter_stands_for_the_grid_and_the_battery_for_itself(void)
{
    energy_dev_now_t now;
    memset(&now, 0, sizeof(now));
    const char *meter = "{\"code\":10000,\"result\":[{\"deviceType\":3,\"totalActivePower\":1360,"
                        "\"dataTime\":\"2026-10-05T13:20:00+02:00\"}]}";
    const char *inverter = "{\"code\":10000,\"result\":[{\"deviceType\":1,\"totalActivePower\":3150,\"gridPower\":2290,"
                           "\"MPPTTotalInputPower\":3420,\"dataTime\":\"2026-10-05T13:17:02+02:00\"}]}";
    const char *battery = "{\"code\":10000,\"result\":[{\"deviceType\":2,\"batterySOC\":64,"
                          "\"chargeDischargePower\":1200}]}";
    TEST_ASSERT_TRUE(energy_dev_parse_realtime(meter, strlen(meter), ENERGY_DEV_METER, 1, &now, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(energy_dev_parse_realtime(inverter, strlen(inverter), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                               sizeof(s_err)));
    TEST_ASSERT_TRUE(energy_dev_parse_realtime(battery, strlen(battery), ENERGY_DEV_BATTERY, 1, &now, s_err,
                                               sizeof(s_err)));
    TEST_ASSERT_TRUE(now.have_meter);
    TEST_ASSERT_EQUAL_DOUBLE(1360, now.feed_w);
    TEST_ASSERT_EQUAL_DOUBLE(64, now.soc);
    TEST_ASSERT_EQUAL_DOUBLE(1200, now.bat_w);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)local(2026, 10, 5, 13, 20), now.at); /* the newest */
}

/* An X3-Hybrid's reply, from the owner's plant (2026-10-06; serials replaced): `totalActivePower` stays 0 while its
 * three phases carry the output, so the phases count; the house draws that output and the grid's 1358 W. */
static void test_a_hybrids_phases_are_its_output_and_the_house_draws_them_and_the_grid(void)
{
    const char *json = "{\"code\":10000,\"result\":[{\"deviceStatus\":102,\"todayImportEnergy\":6.50,"
                       "\"todayExportEnergy\":0.00,\"gridPowerM2\":0.00,\"dataTime\":\"2026-10-06T09:27:38.000+00:00\","
                       "\"plantLocalTime\":\"2026-10-06 11:27:38\",\"deviceSn\":\"H34A10J0000000\","
                       "\"acPower1\":238,\"acPower2\":24,\"acPower3\":1135,\"dailyACOutput\":1.2,\"dailyYield\":1.3,"
                       "\"mpptMap\":{\"MPPT1Voltage\":191.6,\"MPPT1Power\":1471.0,\"MPPT2Power\":0.0},"
                       "\"EPSL1ActivePower\":0,\"EPSL2ActivePower\":0,\"EPSL3ActivePower\":0,\"totalReactivePower\":0,"
                       "\"totalActivePower\":0,\"MPPTTotalInputPower\":1471,\"gridPower\":-1358.0}]}";
    energy_dev_now_t now;
    memset(&now, 0, sizeof(now));
    TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                                       sizeof(s_err)),
                             s_err);
    TEST_ASSERT_EQUAL_DOUBLE(1397, now.ac_w);
    energy_dev_today_t today = { .found = false };
    energy_reading_t r;
    TEST_ASSERT_TRUE(energy_dev_reading(&now, &today, 0, &r));
    TEST_ASSERT_EQUAL_INT32(1471, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(1358, r.grid_w); /* importing */
    TEST_ASSERT_EQUAL_INT32(2755, r.load_w);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)local(2026, 10, 6, 11, 27) + 38, r.at);
}

/* A house on the hybrid's backup (EPS) port draws from it too, as the Token ID's `peps1`–`peps3` count. */
static void test_the_backup_ports_power_is_the_houses_too(void)
{
    const char *json = "{\"code\":10000,\"result\":[{\"acPower1\":0,\"acPower2\":0,\"acPower3\":0,"
                       "\"EPSL1ActivePower\":310,\"EPSL2ActivePower\":0,\"EPSL3ActivePower\":72,"
                       "\"MPPTTotalInputPower\":400,\"gridPower\":0}]}";
    energy_dev_now_t now;
    memset(&now, 0, sizeof(now));
    TEST_ASSERT_TRUE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                               sizeof(s_err)));
    energy_reading_t r;
    TEST_ASSERT_TRUE(energy_dev_reading(&now, NULL, 1791205100, &r));
    TEST_ASSERT_EQUAL_INT32(382, r.load_w);
    TEST_ASSERT_EQUAL_INT32(0, r.grid_w);
}

/* SolaX's dataTime in the forms a reply may take: ISO 8601 with its zone, with or without the offset's colon; the
 * plant's local time with a space or with slashes; seconds or milliseconds since 1970. */
static void test_the_data_time_in_its_forms(void)
{
    time_t want = local(2026, 10, 5, 13, 17) + 2;
    char seconds[24], millis[24];
    snprintf(seconds, sizeof(seconds), "%lld", (long long)want);
    snprintf(millis, sizeof(millis), "\"%lld000\"", (long long)want);
    const char *const k_times[] = { "\"2026-10-05T13:17:02+02:00\"", "\"2026-10-05T19:17:02+0800\"",
                                    "\"2026-10-05T11:17:02Z\"", "\"2026-10-05 13:17:02\"", "\"2026/10/05 13:17:02\"",
                                    seconds, millis };
    for (size_t i = 0; i < sizeof(k_times) / sizeof(k_times[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"code\":10000,\"result\":[{\"MPPTTotalInputPower\":3420,\"dataTime\":%s}]}",
                 k_times[i]);
        energy_dev_now_t now;
        memset(&now, 0, sizeof(now));
        TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                                           sizeof(s_err)),
                                 k_times[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)want, now.at, k_times[i]);
    }
}

/* A dataTime the step can't read refuses the reply: a reading of unknown age would look fresh for ever (the sync's
 * clock would date it), and a dongle that stopped uploading would never show as stale. None at all, null or empty,
 * leaves the sync's clock to date the reading. A refused battery or meter leaves the values gathered so far alone. */
static void test_a_data_time_it_cant_read_refuses_the_reply(void)
{
    const char *const k_times[] = { "\"soon\"", "20261005", "\"2026-13-05 13:17:02\"", "\"05.10.2026 13:17\"", "true" };
    for (size_t i = 0; i < sizeof(k_times) / sizeof(k_times[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"code\":10000,\"result\":[{\"MPPTTotalInputPower\":3420,\"dataTime\":%s}]}",
                 k_times[i]);
        energy_dev_now_t now;
        memset(&now, 0, sizeof(now));
        TEST_ASSERT_FALSE_MESSAGE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                                            sizeof(s_err)),
                                  k_times[i]);
        TEST_ASSERT_EQUAL_STRING("unreadable dataTime", s_err);
    }
    const char *const k_none[] = { "null", "\"\"" };
    for (size_t i = 0; i < sizeof(k_none) / sizeof(k_none[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"code\":10000,\"result\":[{\"MPPTTotalInputPower\":3420,\"dataTime\":%s}]}",
                 k_none[i]);
        energy_dev_now_t now;
        memset(&now, 0, sizeof(now));
        TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_realtime(json, strlen(json), ENERGY_DEV_INVERTER, 1, &now, s_err,
                                                           sizeof(s_err)),
                                 k_none[i]);
        TEST_ASSERT_EQUAL_UINT32(0, now.at);
    }
    energy_dev_now_t now = { .have_soc = true, .soc = 64 };
    const char *battery = "{\"code\":10000,\"result\":[{\"batterySOC\":12,\"dataTime\":\"soon\"}]}";
    TEST_ASSERT_FALSE(energy_dev_parse_realtime(battery, strlen(battery), ENERGY_DEV_BATTERY, 1, &now, s_err,
                                                sizeof(s_err)));
    TEST_ASSERT_EQUAL_DOUBLE(64, now.soc);
}

/* --- today's totals --- */

static void test_todays_row_of_the_month(void)
{
    const char *json = "{\"code\":10000,\"result\":{\"plantEnergyStatDataList\":["
                       "{\"date\":\"2026-10-04\",\"pvGeneration\":21.3,\"exportEnergy\":12.0},"
                       "{\"date\":\"2026-10-05\",\"pvGeneration\":18.4,\"exportEnergy\":9.1,\"importEnergy\":1.2,"
                       "\"loadConsumption\":7.9,\"batteryCharged\":3.0}]}}";
    energy_dev_today_t today;
    TEST_ASSERT_TRUE_MESSAGE(energy_dev_parse_today(json, strlen(json), 2026, 10, 5, &today, s_err, sizeof(s_err)),
                             s_err);
    TEST_ASSERT_TRUE(today.found);
    TEST_ASSERT_EQUAL_DOUBLE(18.4, today.pv_kwh);
    TEST_ASSERT_EQUAL_DOUBLE(9.1, today.to_kwh);
    TEST_ASSERT_EQUAL_DOUBLE(1.2, today.from_kwh);
    TEST_ASSERT_EQUAL_DOUBLE(7.9, today.load_kwh);
    TEST_ASSERT_TRUE(today.have_pv && today.have_to && today.have_from && today.have_load);
    TEST_ASSERT_TRUE(energy_dev_parse_today(json, strlen(json), 2026, 10, 4, &today, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(today.found && today.have_to);
    TEST_ASSERT_FALSE(today.have_from); /* a value the row hasn't is missing, not 0 */
    TEST_ASSERT_TRUE(energy_dev_parse_today(json, strlen(json), 2026, 10, 6, &today, s_err, sizeof(s_err)));
    TEST_ASSERT_FALSE(today.found); /* the day hasn't a row yet */
}

/* A row's day may come as "2026/10/05", as a time stamp in ms of its local midnight, or only by its place. */
static void test_a_rows_day_in_its_other_forms(void)
{
    char json[512];
    energy_dev_today_t today;
    const char *slash = "{\"code\":10000,\"result\":{\"plantEnergyStatDataList\":[{\"date\":\"2026/10/05\","
                        "\"pvGeneration\":\"18.4\"}]}}";
    TEST_ASSERT_TRUE(energy_dev_parse_today(slash, strlen(slash), 2026, 10, 5, &today, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(today.found);
    TEST_ASSERT_EQUAL_DOUBLE(18.4, today.pv_kwh);
    snprintf(json, sizeof(json), "{\"code\":10000,\"result\":{\"plantEnergyStatDataList\":[{\"date\":%lld000,"
             "\"pvGeneration\":5}]}}", (long long)local(2026, 10, 5, 0, 0));
    TEST_ASSERT_TRUE(energy_dev_parse_today(json, strlen(json), 2026, 10, 5, &today, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(today.found);
    TEST_ASSERT_EQUAL_DOUBLE(5, today.pv_kwh);
    const char *by_place = "{\"code\":10000,\"result\":{\"plantEnergyStatDataList\":[{\"pvGeneration\":1},"
                           "{\"pvGeneration\":2},{\"pvGeneration\":3}]}}";
    TEST_ASSERT_TRUE(energy_dev_parse_today(by_place, strlen(by_place), 2026, 10, 3, &today, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(today.found);
    TEST_ASSERT_EQUAL_DOUBLE(3, today.pv_kwh);
}

/* --- the reading --- */

/* In our signs: from the grid +, charging +; the house is the inverter's output less what went to the grid. Today's
 * totals are the statistics' own. */
static void test_the_reading_in_our_signs_with_todays_totals(void)
{
    energy_dev_now_t now = { .have_pv = true, .have_ac = true, .have_grid = true, .have_bat = true, .have_soc = true,
                             .have_yield = true, .pv_w = 3420, .ac_w = 3150, .feed_w = 2290, .bat_w = 1200,
                             .soc = 64, .yield_kwh = 18.3, .at = 1791205022 };
    energy_dev_today_t today = { .found = true, .have_pv = true, .have_to = true, .have_from = true, .have_load = true,
                                 .pv_kwh = 18.4, .to_kwh = 9.1, .from_kwh = 1.2, .load_kwh = 7.9 };
    energy_reading_t r;
    TEST_ASSERT_TRUE(energy_dev_reading(&now, &today, 1791205100, &r));
    TEST_ASSERT_EQUAL_UINT32(1791205022, r.at);
    TEST_ASSERT_EQUAL_INT32(3420, r.pv_w);
    TEST_ASSERT_EQUAL_INT32(-2290, r.grid_w);
    TEST_ASSERT_EQUAL_INT32(860, r.load_w);
    TEST_ASSERT_EQUAL_INT32(1200, r.bat_w);
    TEST_ASSERT_EQUAL_INT16(64, r.soc);
    TEST_ASSERT_TRUE(r.today);
    TEST_ASSERT_EQUAL_UINT32(18400, r.yield_wh);
    TEST_ASSERT_EQUAL_UINT32(9100, r.to_grid_wh);
    TEST_ASSERT_EQUAL_UINT32(1200, r.from_grid_wh);
}

/* Without today's row the inverter counts what was produced and the totals to and from the grid are missing; without
 * a battery its charge is -1; without a dataTime the sync's clock dates it. */
static void test_the_reading_without_the_statistics_or_a_battery(void)
{
    energy_dev_now_t now = { .have_pv = true, .have_ac = true, .have_grid = true, .have_yield = true, .pv_w = 900,
                             .ac_w = 880, .feed_w = -400, .yield_kwh = 2.5 };
    energy_dev_today_t today = { .found = false };
    energy_reading_t r;
    TEST_ASSERT_TRUE(energy_dev_reading(&now, &today, 1791205100, &r));
    TEST_ASSERT_EQUAL_UINT32(1791205100, r.at);
    TEST_ASSERT_EQUAL_INT32(400, r.grid_w);   /* importing */
    TEST_ASSERT_EQUAL_INT32(1280, r.load_w);
    TEST_ASSERT_EQUAL_INT32(0, r.bat_w);
    TEST_ASSERT_EQUAL_INT16(-1, r.soc);
    TEST_ASSERT_EQUAL_UINT32(2500, r.yield_wh);
    TEST_ASSERT_TRUE(r.today);
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, r.to_grid_wh);
    energy_dev_now_t nothing = { 0 };
    TEST_ASSERT_FALSE(energy_dev_reading(&nothing, &today, 1791205100, &r));
}

static void test_replies_that_say_they_failed(void)
{
    static const char *const k_cases[][2] = {
        { "not json", "not a JSON object" },
        { "{\"code\":10401,\"message\":\"access token is invalid\"}", "access token is invalid" },
        { "{\"code\":10500}", "code 10500" },
        { "{\"message\":\"success\"}", "no code" },
        { "{\"code\":10000,\"result\":[]}", "no data" },
        { "{\"code\":10000,\"result\":[[[[[[[[[[1]]]]]]]]]]}", "nested too deeply" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        energy_dev_now_t now;
        memset(&now, 0, sizeof(now));
        TEST_ASSERT_FALSE_MESSAGE(energy_dev_parse_realtime(k_cases[i][0], strlen(k_cases[i][0]), ENERGY_DEV_INVERTER,
                                                            1, &now, s_err, sizeof(s_err)),
                                  k_cases[i][0]);
        TEST_ASSERT_EQUAL_STRING(k_cases[i][1], s_err);
    }
}

/* A refusal is SolaX turning a request down: a kept token or plant may be stale, so one more try after a new login
 * can help (spec §11.6). A timeout or the radio's budget (no reply), a rate limit, a server's error, or a reply that
 * answered as it should but held nothing is not one: trying again at once wouldn't help. */
static void test_a_refusal_is_401_403_or_a_reply_with_another_code(void)
{
    const char *bad = "{\"code\":10401,\"message\":\"access token is invalid\"}";
    const char *bad_text = "{\"code\":\"10402\"}";
    const char *empty = "{\"code\":10000,\"result\":[]}";
    const char *no_code = "{\"message\":\"success\"}";
    TEST_ASSERT_TRUE(energy_dev_refused(401, "", 0));
    TEST_ASSERT_TRUE(energy_dev_refused(403, NULL, 0));
    TEST_ASSERT_TRUE(energy_dev_refused(200, bad, strlen(bad)));
    TEST_ASSERT_TRUE(energy_dev_refused(200, bad_text, strlen(bad_text)));
    TEST_ASSERT_FALSE(energy_dev_refused(429, bad, strlen(bad))); /* a rate limit: another try at once makes it worse */
    TEST_ASSERT_FALSE(energy_dev_refused(500, bad, strlen(bad)));
    TEST_ASSERT_FALSE(energy_dev_refused(0, "", 0));  /* no reply */
    TEST_ASSERT_FALSE(energy_dev_refused(-1, "", 0)); /* a kept connection that brought nothing */
    TEST_ASSERT_FALSE(energy_dev_refused(200, empty, strlen(empty)));
    TEST_ASSERT_FALSE(energy_dev_refused(200, "not json", 8));
    TEST_ASSERT_FALSE(energy_dev_refused(200, no_code, strlen(no_code)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_url_names_the_region);
    RUN_TEST(test_the_token_request_carries_the_credentials);
    RUN_TEST(test_a_token_reply_gives_the_token_and_its_life);
    RUN_TEST(test_token_refusals);
    RUN_TEST(test_a_token_is_renewed_ten_minutes_before_it_ends);
    RUN_TEST(test_the_requests_name_the_plant_and_the_device);
    RUN_TEST(test_the_first_plant_and_its_devices);
    RUN_TEST(test_the_inverter_gives_the_panels_its_output_and_the_grid);
    RUN_TEST(test_without_a_total_the_mppts_add_up_and_a_commercial_plant_is_in_kw);
    RUN_TEST(test_the_meter_stands_for_the_grid_and_the_battery_for_itself);
    RUN_TEST(test_a_hybrids_phases_are_its_output_and_the_house_draws_them_and_the_grid);
    RUN_TEST(test_the_backup_ports_power_is_the_houses_too);
    RUN_TEST(test_the_data_time_in_its_forms);
    RUN_TEST(test_a_data_time_it_cant_read_refuses_the_reply);
    RUN_TEST(test_todays_row_of_the_month);
    RUN_TEST(test_a_rows_day_in_its_other_forms);
    RUN_TEST(test_the_reading_in_our_signs_with_todays_totals);
    RUN_TEST(test_the_reading_without_the_statistics_or_a_battery);
    RUN_TEST(test_replies_that_say_they_failed);
    RUN_TEST(test_a_refusal_is_401_403_or_a_reply_with_another_code);
    return UNITY_END();
}
