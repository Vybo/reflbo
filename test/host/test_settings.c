#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "unity.h"

static settings_t s_defaults, s_out;
static char s_err[96];
static char s_json[2048];

void setUp(void)
{
    s_defaults = (settings_t){ .language = "en", .clock_24h = true, .tz_posix = "CET-1CEST,M3.5.0,M10.5.0/3",
                               .tz_iana = "Europe/Prague", .sensors_every_min = 5, .display_every_min = 1,
                               .lpm_quarter_hz = 4 };
    memset(&s_out, 0xAA, sizeof(s_out));
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_the_spec_sketch_parses(void)
{
    const char *json =
        "{ \"schema\": 1, \"language\": \"en\","
        "  \"location\": { \"name\": \"Brno\", \"lat\": 49.1951, \"lon\": 16.6068 },"
        "  \"time\": { \"tz_iana\": \"Europe/London\", \"tz_posix\": \"GMT0BST,M3.5.0/1,M10.5.0\","
        "            \"clock_24h\": false, \"ntp\": [\"cz.pool.ntp.org\"] },"
        "  \"units\": { \"temp\": \"F\" },"
        "  \"sensors\": { \"interval_min\": 10, \"temp_offset_c\": -3.5, \"hum_offset_pct\": 2.25 },"
        "  \"display\": { \"contrast\": \"default\", \"update_min\": 2, \"lpm_hz\": 0.25 } }";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("Europe/London", s_out.tz_iana);
    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", s_out.tz_posix);
    TEST_ASSERT_FALSE(s_out.clock_24h);
    TEST_ASSERT_TRUE(s_out.fahrenheit);
    TEST_ASSERT_EQUAL_UINT8(10, s_out.sensors_every_min);
    TEST_ASSERT_EQUAL_INT16(-350, s_out.temp_offset_c100);
    TEST_ASSERT_EQUAL_INT16(225, s_out.hum_offset_pct100);
    TEST_ASSERT_EQUAL_UINT8(2, s_out.display_every_min);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.lpm_quarter_hz);
}

static void test_missing_keys_take_the_defaults(void)
{
    TEST_ASSERT_TRUE(settings_from_json("{\"schema\": 1}", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s_defaults, &s_out, sizeof(s_out));
}

static void test_bad_values_are_clamped_or_ignored_one_by_one(void)
{
    const char *json = "{\"schema\": 1, \"time\": {\"clock_24h\": \"yes\", \"tz_posix\": 5},"
                       " \"sensors\": {\"interval_min\": 99, \"temp_offset_c\": -50},"
                       " \"display\": {\"update_min\": 0, \"lpm_hz\": 3}, \"language\": \"much too long\"}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(s_out.clock_24h);                      /* wrong type: default */
    TEST_ASSERT_EQUAL_STRING(s_defaults.tz_posix, s_out.tz_posix);
    TEST_ASSERT_EQUAL_UINT8(30, s_out.sensors_every_min);   /* clamped */
    TEST_ASSERT_EQUAL_INT16(-1000, s_out.temp_offset_c100); /* clamped to -10 °C */
    TEST_ASSERT_EQUAL_UINT8(1, s_out.display_every_min);
    TEST_ASSERT_EQUAL_UINT8(4, s_out.lpm_quarter_hz);       /* 3 Hz is not a panel rate */
    TEST_ASSERT_EQUAL_STRING("en", s_out.language);
}

static void test_not_json_or_another_schema_fails(void)
{
    TEST_ASSERT_FALSE(settings_from_json("{", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("not a JSON object", s_err);
    TEST_ASSERT_FALSE(settings_from_json("[1]", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_FALSE(settings_from_json("{\"schema\": 2}", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("schema must be 1", s_err);
}

static void test_deep_nesting_is_rejected_before_parsing(void)
{
    static char json[256];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"schema\": 1, \"x\": ");
    for (int i = 0; i < 40; i++) {
        json[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        json[n++] = ']';
    }
    snprintf(json + n, sizeof(json) - n, "}");
    TEST_ASSERT_FALSE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
}

static void test_saving_keeps_keys_this_firmware_does_not_know(void)
{
    const char *base = "{\"schema\": 1, \"mqtt\": {\"host\": \"ha.local\"}, \"time\": {\"ntp\": [\"a\"], \"clock_24h\": true}}";
    settings_t s = s_defaults;
    s.clock_24h = false;
    s.lpm_quarter_hz = 32;
    TEST_ASSERT_TRUE(settings_to_json(&s, base, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "ha.local"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"ntp\""));
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_FALSE(s_out.clock_24h);
    TEST_ASSERT_EQUAL_UINT8(32, s_out.lpm_quarter_hz);
}

static void test_saving_without_a_base_round_trips(void)
{
    settings_t s = s_defaults;
    s.fahrenheit = true;
    s.temp_offset_c100 = -125;
    TEST_ASSERT_TRUE(settings_to_json(&s, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s, &s_out, sizeof(s));
    TEST_ASSERT_EQUAL_UINT(0, settings_to_json(&s, NULL, s_json, 16));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_spec_sketch_parses);
    RUN_TEST(test_missing_keys_take_the_defaults);
    RUN_TEST(test_bad_values_are_clamped_or_ignored_one_by_one);
    RUN_TEST(test_not_json_or_another_schema_fails);
    RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
    RUN_TEST(test_saving_keeps_keys_this_firmware_does_not_know);
    RUN_TEST(test_saving_without_a_base_round_trips);
    return UNITY_END();
}
