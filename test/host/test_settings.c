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
                               .lpm_quarter_hz = 4, .place = "Brno", .lat_e4 = 491951, .lon_e4 = 166068,
                               .bat_cal = SETTINGS_BAT_CURVE, .bat_empty_mv = 3270, .bat_full_mv = 4200 };
    memset(&s_out, 0xAA, sizeof(s_out));
    s_err[0] = '\0';
}

void tearDown(void) {}

/* "<prefix>" + `levels` brackets opened and closed + "}": a document nested past the cap. */
static const char *nested(const char *prefix, int levels)
{
    static char text[256];
    size_t n = (size_t)snprintf(text, sizeof(text), "%s", prefix);
    for (int i = 0; i < levels; i++) {
        text[n++] = '[';
    }
    for (int i = 0; i < levels; i++) {
        text[n++] = ']';
    }
    snprintf(text + n, sizeof(text) - n, "}");
    return text;
}

static void test_the_spec_sketch_parses(void)
{
    const char *json =
        "{ \"schema\": 1, \"language\": \"en\","
        "  \"location\": { \"name\": \"Kraków\", \"lat\": 50.0647, \"lon\": -19.945 },"
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
    TEST_ASSERT_EQUAL_STRING("Kraków", s_out.place);
    TEST_ASSERT_EQUAL_INT32(500647, s_out.lat_e4);
    TEST_ASSERT_EQUAL_INT32(-199450, s_out.lon_e4);
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

static void test_the_location_is_clamped_to_the_globe(void)
{
    const char *json = "{\"schema\": 1, \"location\": {\"name\": \"\", \"lat\": 95, \"lon\": -200.5}}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("Brno", s_out.place); /* an empty name keeps the default */
    TEST_ASSERT_EQUAL_INT32(900000, s_out.lat_e4);
    TEST_ASSERT_EQUAL_INT32(-1800000, s_out.lon_e4);
}

/* PATCH /api/settings (spec §10.3): the web UI sends only what a page changed. */
static void test_a_patch_changes_only_what_it_names(void)
{
    const char *base = "{\"schema\": 1, \"language\": \"cs\", \"mqtt\": {\"host\": \"ha.local\"},"
                       " \"time\": {\"tz_iana\": \"Europe/Prague\", \"clock_24h\": false, \"ntp\": [\"a\"]}}";
    const char *patch = "{\"time\": {\"tz_iana\": \"Europe/London\", \"tz_posix\": \"GMT0BST,M3.5.0/1,M10.5.0\"},"
                        " \"location\": {\"name\": \"Kraków\", \"lat\": 50.0647, \"lon\": 19.945}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_patch(base, patch, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0, s_err);
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("cs", s_out.language); /* not in the patch */
    TEST_ASSERT_FALSE(s_out.clock_24h);              /* beside a patched key */
    TEST_ASSERT_EQUAL_STRING("Europe/London", s_out.tz_iana);
    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", s_out.tz_posix);
    TEST_ASSERT_EQUAL_STRING("Kraków", s_out.place);
    TEST_ASSERT_EQUAL_INT32(199450, s_out.lon_e4);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "ha.local")); /* keys this firmware doesn't know */
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"ntp\""));
}

/* RFC 7396: objects merge, null removes a key, anything else replaces what was there. */
static void test_a_patch_follows_json_merge_patch(void)
{
    const char *base = "{\"schema\": 1, \"time\": {\"ntp\": [\"a\", \"b\"], \"clock_24h\": false}, \"x\": {\"y\": 1}}";
    const char *patch = "{\"time\": {\"ntp\": [\"c\"], \"clock_24h\": null}, \"x\": 5}";
    TEST_ASSERT_TRUE(settings_patch(base, patch, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"time\":{\"ntp\":[\"c\"]},\"x\":5}", s_json);
    TEST_ASSERT_TRUE(settings_patch(NULL, "{\"language\": \"cs\"}", s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"language\":\"cs\"}", s_json); /* no file yet */
}

static void test_a_patch_must_be_an_object_that_keeps_the_schema(void)
{
    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "[1]", s_json, sizeof(s_json), s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("the patch must be a JSON object", s_err);
    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"schema\": 2}", s_json, sizeof(s_json), s_err,
                                             sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("schema must be 1", s_err);
    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"schema\": null}", s_json, sizeof(s_json), s_err,
                                             sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", nested("{\"x\": ", 20), s_json, sizeof(s_json), s_err,
                                             sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"language\": \"cs\"}", s_json, 8, s_err,
                                             sizeof(s_err))); /* no room */
}

/* A file nested past the cap is no base for the save and patch paths either: the loader rejects
 * it, and cJSON's recursion could overrun the app task's stack (M3b review). */
static void test_a_base_nested_too_deep_is_not_parsed(void)
{
    const char *base = nested("{\"schema\": 1, \"junk\": ", 20);
    settings_t s = s_defaults;
    TEST_ASSERT_TRUE(settings_to_json(&s, base, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NULL(strstr(s_json, "junk"));
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s, &s_out, sizeof(s));
    TEST_ASSERT_TRUE_MESSAGE(settings_patch(base, "{\"language\": \"cs\"}", s_json, sizeof(s_json), s_err,
                                            sizeof(s_err)) > 0, s_err);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"language\":\"cs\"}", s_json);
}

/* Battery calibration (owner, 2026-09-30): the built-in curve or the owner's own voltages. */
static void test_the_battery_calibration_parses_and_round_trips(void)
{
    const char *json = "{\"schema\": 1, \"battery\": {\"level_from\": \"manual\", \"empty_v\": 3.45, \"full_v\": 4.12}}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BAT_MANUAL, s_out.bat_cal);
    TEST_ASSERT_EQUAL_UINT16(3450, s_out.bat_empty_mv);
    TEST_ASSERT_EQUAL_UINT16(4120, s_out.bat_full_mv);
    settings_t s = s_out;
    TEST_ASSERT_TRUE(settings_to_json(&s, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s, &s_out, sizeof(s));
}

static void test_a_battery_calibration_without_room_keeps_the_curve(void)
{
    const char *json = "{\"schema\": 1, \"battery\": {\"level_from\": \"manual\", \"empty_v\": 3.9, \"full_v\": 4.0}}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BAT_CURVE, s_out.bat_cal);
    TEST_ASSERT_EQUAL_UINT16(3270, s_out.bat_empty_mv);
    TEST_ASSERT_EQUAL_UINT16(4200, s_out.bat_full_mv);
    json = "{\"schema\": 1, \"battery\": {\"level_from\": \"guesswork\", \"empty_v\": 3.4, \"full_v\": 4.1}}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BAT_CURVE, s_out.bat_cal); /* an unknown method, maybe a later firmware's */
    TEST_ASSERT_EQUAL_UINT16(3400, s_out.bat_empty_mv);         /* the voltages are good, and kept */
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
    RUN_TEST(test_the_location_is_clamped_to_the_globe);
    RUN_TEST(test_a_patch_changes_only_what_it_names);
    RUN_TEST(test_a_patch_follows_json_merge_patch);
    RUN_TEST(test_a_patch_must_be_an_object_that_keeps_the_schema);
    RUN_TEST(test_a_base_nested_too_deep_is_not_parsed);
    RUN_TEST(test_the_battery_calibration_parses_and_round_trips);
    RUN_TEST(test_a_battery_calibration_without_room_keeps_the_curve);
    return UNITY_END();
}
