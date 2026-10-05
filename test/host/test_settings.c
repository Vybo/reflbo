#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "unity.h"

static settings_t s_defaults, s_out;
static char s_err[96];
static char s_json[SETTINGS_FILE_MAX];

void setUp(void)
{
    s_defaults = (settings_t){ .language = "en", .clock_24h = true, .tz_posix = "CET-1CEST,M3.5.0,M10.5.0/3",
                               .tz_iana = "Europe/Prague", .sensors_every_min = 5, .display_every_min = 1,
                               .lpm_quarter_hz = 4, .place = "Brno", .lat_e4 = 491951, .lon_e4 = 166068,
                               .bat_cal = SETTINGS_BAT_CURVE, .bat_empty_mv = 3270, .bat_full_mv = 4200,
                               .sync_mode = SETTINGS_SYNC_TIMES, .sync_time_count = 1, .sync_times = { 330 },
                               .sync_interval_min = 60, .quiet = false, .quiet_from = 1380, .quiet_to = 360,
                               .ntp = { "cz.pool.ntp.org", "pool.ntp.org" } };
    settings_radar_defaults(&s_defaults);
    settings_solar_defaults(&s_defaults);
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

/* A curve learned from a full discharge (D21), kept with the time it was learned. */
static void test_a_learned_battery_curve_parses_and_round_trips(void)
{
    char json[512];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"schema\": 1, \"battery\": {\"level_from\": \"learned\", "
                                                    "\"learned_at\": 1790786009, \"learned_mv\": [");
    for (int p = 0; p < SETTINGS_BAT_CURVE_POINTS; p++) {
        n += (size_t)snprintf(json + n, sizeof(json) - n, "%s%d", p ? ", " : "", 3300 + 45 * p);
    }
    snprintf(json + n, sizeof(json) - n, "]}}");
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BAT_LEARNED, s_out.bat_cal);
    TEST_ASSERT_EQUAL_UINT16(3750, s_out.bat_learned_mv[10]);
    TEST_ASSERT_EQUAL_UINT32(1790786009u, s_out.bat_learned_at);
    settings_t s = s_out;
    TEST_ASSERT_TRUE(settings_to_json(&s, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s, &s_out, sizeof(s));
}

static void test_learned_without_a_usable_curve_keeps_the_built_in_one(void)
{
    const char *json = "{\"schema\": 1, \"battery\": {\"level_from\": \"learned\", \"learned_mv\": [3300, 3200]}}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BAT_CURVE, s_out.bat_cal);
    TEST_ASSERT_EQUAL_UINT16(0, s_out.bat_learned_mv[0]); /* not taken */
}

static void test_the_sync_settings_parse_and_round_trip(void)
{
    const char *json = "{\"schema\":1,\"time\":{\"ntp\":[\"ntp.nic.cz\"]},"
                       "\"sync\":{\"mode\":\"interval\",\"times\":[\"07:15\",\"19:45\"],\"interval_min\":30,"
                       "\"quiet\":{\"enabled\":true,\"from\":\"22:30\",\"to\":\"06:15\"}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s_out.sync_mode);
    TEST_ASSERT_EQUAL_UINT8(2, s_out.sync_time_count);
    TEST_ASSERT_EQUAL_UINT16(435, s_out.sync_times[0]);
    TEST_ASSERT_EQUAL_UINT16(1185, s_out.sync_times[1]);
    TEST_ASSERT_EQUAL_UINT16(30, s_out.sync_interval_min);
    TEST_ASSERT_TRUE(s_out.quiet);
    TEST_ASSERT_EQUAL_UINT16(1350, s_out.quiet_from);
    TEST_ASSERT_EQUAL_UINT16(375, s_out.quiet_to);
    TEST_ASSERT_EQUAL_STRING("ntp.nic.cz", s_out.ntp[0]);
    TEST_ASSERT_EQUAL_STRING("", s_out.ntp[1]);

    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    settings_t again;
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"from\":\t\"22:30\""));
}

static void test_sync_times_are_sorted_without_repeats_or_bad_ones(void)
{
    const char *json = "{\"schema\":1,\"sync\":{\"times\":[\"23:00\",\"05:30\",\"05:30\",\"25:00\",\"7:5\",5,"
                       "\"07:05\",\"00:00\",\"01:00\",\"02:00\",\"03:00\",\"04:00\",\"06:00\",\"08:00\"]}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    static const uint16_t k_kept[] = { 0, 60, 120, 180, 240, 330, 360, 425 }; /* the first 8 of the day */
    TEST_ASSERT_EQUAL_UINT8(8, s_out.sync_time_count);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(k_kept, s_out.sync_times, 8);
}

static void test_sync_settings_fall_back_one_by_one(void)
{
    const char *json = "{\"schema\":1,\"time\":{\"ntp\":[\"\",\"bad host\",17]},"
                       "\"sync\":{\"mode\":\"sometimes\",\"times\":[],\"interval_min\":5,"
                       "\"quiet\":{\"enabled\":\"yes\",\"from\":\"later\",\"to\":\"07:00\"}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_TIMES, s_out.sync_mode);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.sync_time_count);
    TEST_ASSERT_EQUAL_UINT16(330, s_out.sync_times[0]);
    TEST_ASSERT_EQUAL_UINT16(15, s_out.sync_interval_min);
    TEST_ASSERT_FALSE(s_out.quiet);
    TEST_ASSERT_EQUAL_UINT16(1380, s_out.quiet_from);
    TEST_ASSERT_EQUAL_UINT16(420, s_out.quiet_to);
    TEST_ASSERT_EQUAL_STRING("cz.pool.ntp.org", s_out.ntp[0]); /* no usable server: the defaults */
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", s_out.ntp[1]);
    json = "{\"schema\":1,\"sync\":{\"mode\":\"always\",\"interval_min\":99999}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_ALWAYS, s_out.sync_mode);
    TEST_ASSERT_EQUAL_UINT16(1440, s_out.sync_interval_min);
}

static void test_the_sync_defaults_are_the_specs(void)
{
    settings_t defaults;
    memset(&defaults, 0, sizeof(defaults));
    settings_sync_defaults(&defaults); /* spec §14.3 */
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_TIMES, defaults.sync_mode);
    TEST_ASSERT_EQUAL_UINT8(1, defaults.sync_time_count);
    TEST_ASSERT_EQUAL_UINT16(330, defaults.sync_times[0]);
    TEST_ASSERT_EQUAL_UINT16(60, defaults.sync_interval_min);
    TEST_ASSERT_FALSE(defaults.quiet);
    TEST_ASSERT_EQUAL_UINT16(1380, defaults.quiet_from);
    TEST_ASSERT_EQUAL_UINT16(360, defaults.quiet_to);
    TEST_ASSERT_EQUAL_STRING("cz.pool.ntp.org", defaults.ntp[0]);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", defaults.ntp[1]);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_TIMES, defaults.sync_mode_before_always); /* M6b (D31) */
    /* A file saved before M5 has neither sync.* nor time.ntp: it syncs at 05:30 */
    const char *json = "{\"schema\":1,\"language\":\"cs\",\"time\":{\"clock_24h\":false}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.sync_time_count);
    TEST_ASSERT_EQUAL_UINT16(330, s_out.sync_times[0]);
    TEST_ASSERT_EQUAL_STRING("cz.pool.ntp.org", s_out.ntp[0]);
}

static void test_the_radars_default_to_the_location(void)
{
    settings_t defaults;
    memset(&defaults, 0, sizeof(defaults));
    settings_radar_defaults(&defaults); /* spec §14.3 */
    TEST_ASSERT_FALSE(defaults.wx_centre_set);
    TEST_ASSERT_EQUAL_UINT8(26, defaults.wx_zoom_q); /* 6.5 */
    TEST_ASSERT_FALSE(defaults.fl_centre_set);
    TEST_ASSERT_EQUAL_UINT8(50, defaults.fl_range_km);
    TEST_ASSERT_EQUAL_UINT16(0, defaults.fl_min_alt_ft);
    TEST_ASSERT_FALSE(defaults.fl_ground);
    TEST_ASSERT_EQUAL_UINT8(100, defaults.fl_max);
    /* A file from before M6: both centres sit on its location, and move with it */
    const char *json = "{\"schema\":1,\"location\":{\"name\":\"Ostrava\",\"lat\":49.8209,\"lon\":18.2625}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(s_out.wx_centre_set);
    TEST_ASSERT_EQUAL_INT32(498209, s_out.wx_lat_e4);
    TEST_ASSERT_EQUAL_INT32(182625, s_out.wx_lon_e4);
    TEST_ASSERT_EQUAL_INT32(498209, s_out.fl_lat_e4);
    TEST_ASSERT_EQUAL_INT32(182625, s_out.fl_lon_e4);
}

static void test_the_radar_settings_parse_clamp_and_round_trip(void)
{
    const char *json = "{\"schema\":1,\"radar\":{"
                       "\"weather\":{\"lat\":50.0755,\"lon\":14.4378,\"zoom\":7.3},"
                       "\"flights\":{\"lat\":48.1103,\"lon\":16.5697,\"range_km\":25,\"min_alt_ft\":3000,"
                       "\"ground\":true,\"max\":40}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_out.wx_centre_set);
    TEST_ASSERT_EQUAL_INT32(500755, s_out.wx_lat_e4);
    TEST_ASSERT_EQUAL_INT32(144378, s_out.wx_lon_e4);
    TEST_ASSERT_EQUAL_UINT8(29, s_out.wx_zoom_q); /* 7.3 to the nearest quarter: 7.25 */
    TEST_ASSERT_TRUE(s_out.fl_centre_set);
    TEST_ASSERT_EQUAL_INT32(481103, s_out.fl_lat_e4);
    TEST_ASSERT_EQUAL_UINT8(25, s_out.fl_range_km);
    TEST_ASSERT_EQUAL_UINT16(3000, s_out.fl_min_alt_ft);
    TEST_ASSERT_TRUE(s_out.fl_ground);
    TEST_ASSERT_EQUAL_UINT8(40, s_out.fl_max);

    settings_t again;
    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"zoom\":\t7.25"));
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));

    json = "{\"schema\":1,\"radar\":{\"weather\":{\"zoom\":12},"
           "\"flights\":{\"range_km\":5,\"min_alt_ft\":70000,\"max\":0}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(36, s_out.wx_zoom_q); /* 9 at most */
    TEST_ASSERT_EQUAL_UINT8(10, s_out.fl_range_km);
    TEST_ASSERT_EQUAL_UINT16(60000, s_out.fl_min_alt_ft);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.fl_max);
    json = "{\"schema\":1,\"radar\":{\"weather\":{\"zoom\":1,\"lat\":50.1},"
           "\"flights\":{\"range_km\":250,\"min_alt_ft\":-5,\"max\":500,\"ground\":\"yes\"}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(16, s_out.wx_zoom_q); /* 4 at least */
    TEST_ASSERT_FALSE(s_out.wx_centre_set);      /* a latitude alone is no centre */
    TEST_ASSERT_EQUAL_INT32(491951, s_out.wx_lat_e4);
    TEST_ASSERT_EQUAL_UINT8(100, s_out.fl_range_km);
    TEST_ASSERT_EQUAL_UINT16(0, s_out.fl_min_alt_ft);
    TEST_ASSERT_EQUAL_UINT8(100, s_out.fl_max);
    TEST_ASSERT_FALSE(s_out.fl_ground);
}

static void test_a_centre_that_follows_the_location_is_not_saved(void)
{
    TEST_ASSERT_TRUE(settings_to_json(&s_defaults, NULL, s_json, sizeof(s_json)) > 0);
    const char *radar = strstr(s_json, "\"radar\"");
    TEST_ASSERT_NOT_NULL(radar);
    TEST_ASSERT_NULL(strstr(radar, "\"lat\"")); /* so a new location moves it */
    TEST_ASSERT_NOT_NULL(strstr(radar, "\"zoom\":\t6.5"));
    TEST_ASSERT_NOT_NULL(strstr(radar, "\"range_km\":\t50"));

    /* The web UI's "use the location": the merge patch removes a centre */
    const char *base = "{\"schema\":1,\"radar\":{\"weather\":{\"lat\":50.0755,\"lon\":14.4378,\"zoom\":7}}}";
    char patched[512];
    TEST_ASSERT_TRUE(settings_patch(base, "{\"radar\":{\"weather\":{\"lat\":null,\"lon\":null}}}", patched,
                                    sizeof(patched), s_err, sizeof(s_err)) > 0);
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(patched, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(s_out.wx_centre_set);
    TEST_ASSERT_EQUAL_INT32(491951, s_out.wx_lat_e4);
    TEST_ASSERT_EQUAL_UINT8(28, s_out.wx_zoom_q);
}

/* sync.mode_before_always (spec §14.3, D31): what BOOT double turns `always` back into; never `always`. */
static void test_the_mode_before_always_parses_and_round_trips(void)
{
    const char *json = "{\"schema\":1,\"sync\":{\"mode\":\"always\",\"mode_before_always\":\"interval\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_ALWAYS, s_out.sync_mode);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s_out.sync_mode_before_always);
    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"mode_before_always\":\t\"interval\""), s_json);
    settings_t again;
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
    static const char *const k_ignored[] = { "\"always\"", "\"sometimes\"", "3" };
    for (size_t i = 0; i < sizeof(k_ignored) / sizeof(k_ignored[0]); i++) {
        snprintf(s_json, sizeof(s_json), "{\"schema\":1,\"sync\":{\"mode_before_always\":%s}}", k_ignored[i]);
        TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_SYNC_TIMES, s_out.sync_mode_before_always, k_ignored[i]);
    }
}

/* BOOT double on the dashboard (spec §5.6, D31): `always` on, remembering the mode before, then back. */
static void test_boot_double_toggles_always_and_back(void)
{
    settings_t s = s_defaults;
    s.sync_mode = SETTINGS_SYNC_INTERVAL;
    settings_toggle_always(&s);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_ALWAYS, s.sync_mode);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s.sync_mode_before_always);
    settings_toggle_always(&s);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s.sync_mode);
    s.sync_mode = SETTINGS_SYNC_MANUAL;
    settings_toggle_always(&s);
    settings_toggle_always(&s);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode);
}

/* The menu or a page that turns `always` on remembers the mode it left too, so BOOT double returns to it. */
static void test_entering_always_remembers_the_mode_it_left(void)
{
    settings_t s = s_defaults;
    s.sync_mode = SETTINGS_SYNC_ALWAYS;
    settings_remember_mode(&s, SETTINGS_SYNC_MANUAL);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
    settings_remember_mode(&s, SETTINGS_SYNC_ALWAYS); /* `always` again: nothing left */
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
    s.sync_mode = SETTINGS_SYNC_INTERVAL; /* leaving `always` keeps what it remembers */
    settings_remember_mode(&s, SETTINGS_SYNC_ALWAYS);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
}

/* A page's settings or a restored backup replace them whole (spec §14.3): entering `always` remembers
 * the mode left, unless the document names a mode_before_always of its own. */
static void test_settings_that_replace_others_remember_the_mode_left(void)
{
    settings_t before = s_defaults;
    before.sync_mode = SETTINGS_SYNC_INTERVAL;
    settings_t next = before; /* the Sync page: the mode alone, merged into the file */
    next.sync_mode = SETTINGS_SYNC_ALWAYS;
    settings_replaced(&next, &before);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, next.sync_mode_before_always);
    next = before; /* a backup taken in `always`, with the mode it had left */
    next.sync_mode = SETTINGS_SYNC_ALWAYS;
    next.sync_mode_before_always = SETTINGS_SYNC_MANUAL;
    settings_replaced(&next, &before);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, next.sync_mode_before_always);
}

static void test_the_solar_defaults_are_the_specs(void)
{
    settings_t defaults;
    memset(&defaults, 0, sizeof(defaults));
    settings_solar_defaults(&defaults); /* spec §14.3 */
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, defaults.sync_steps);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, defaults.solar_source);
    TEST_ASSERT_EQUAL_UINT8(1, defaults.solar_plane_count);
    TEST_ASSERT_EQUAL_UINT16(500, defaults.solar_planes[0].kwp_e2); /* 5 kWp, 35°, south */
    TEST_ASSERT_EQUAL_UINT8(35, defaults.solar_planes[0].tilt);
    TEST_ASSERT_EQUAL_INT16(0, defaults.solar_planes[0].azimuth);
    TEST_ASSERT_EQUAL_UINT8(14, defaults.solar_losses_pct);
    TEST_ASSERT_EQUAL_UINT16(0, defaults.solar_inverter_kw_e2);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_OFF, defaults.energy_source);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_AUTO, defaults.energy_battery);
    /* A file from before M6d runs every step, with no solar source and no inverter */
    const char *json = "{\"schema\":1,\"sync\":{\"mode\":\"interval\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, s_out.sync_steps);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, s_out.solar_source);
}

static void test_the_sync_steps_parse_and_round_trip(void)
{
    const char *json = "{\"schema\":1,\"sync\":{\"steps\":[\"weather\",\"radar\",\"energy\",\"time\",\"tides\"]}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEP_WEATHER | SETTINGS_STEP_RADAR | SETTINGS_STEP_ENERGY, s_out.sync_steps);
    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"steps\":\t[\"weather\", \"radar\", \"energy\"]"));
    settings_t again;
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(s_out.sync_steps, again.sync_steps);
    json = "{\"schema\":1,\"sync\":{\"steps\":[]}}"; /* every data step off: the time still runs */
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(0, s_out.sync_steps);
    json = "{\"schema\":1,\"sync\":{\"steps\":\"all\"}}"; /* not a list: all of them */
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, s_out.sync_steps);
    TEST_ASSERT_EQUAL_STRING("solar", settings_step_name(SETTINGS_STEP_SOLAR));
}

static void test_the_solar_settings_parse_clamp_and_round_trip(void)
{
    const char *json = "{\"schema\":1,\"solar\":{\"source\":\"forecast-solar\",\"planes\":["
                       "{\"kwp\":5.2,\"tilt\":35,\"azimuth\":0},{\"kwp\":2.45,\"tilt\":20,\"azimuth\":-90}],"
                       "\"losses_pct\":10,\"inverter_kw\":4.6},"
                       "\"energy\":{\"source\":\"solax\",\"battery\":\"on\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_FORECAST_SOLAR, s_out.solar_source);
    TEST_ASSERT_EQUAL_UINT8(2, s_out.solar_plane_count);
    TEST_ASSERT_EQUAL_UINT16(520, s_out.solar_planes[0].kwp_e2);
    TEST_ASSERT_EQUAL_UINT16(245, s_out.solar_planes[1].kwp_e2);
    TEST_ASSERT_EQUAL_UINT8(20, s_out.solar_planes[1].tilt);
    TEST_ASSERT_EQUAL_INT16(-90, s_out.solar_planes[1].azimuth);
    TEST_ASSERT_EQUAL_UINT8(10, s_out.solar_losses_pct);
    TEST_ASSERT_EQUAL_UINT16(460, s_out.solar_inverter_kw_e2);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_SOLAX, s_out.energy_source);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_ON, s_out.energy_battery);

    settings_t again;
    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));

    json = "{\"schema\":1,\"solar\":{\"source\":\"sun\",\"planes\":[{\"kwp\":0.01,\"tilt\":95,\"azimuth\":200},"
           "{\"kwp\":150,\"tilt\":-5,\"azimuth\":-200},{\"kwp\":3}],\"losses_pct\":80,\"inverter_kw\":150},"
           "\"energy\":{\"source\":\"fronius\",\"battery\":\"maybe\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, s_out.solar_source); /* unknown: the default */
    TEST_ASSERT_EQUAL_UINT8(2, s_out.solar_plane_count);             /* two at most */
    TEST_ASSERT_EQUAL_UINT16(10, s_out.solar_planes[0].kwp_e2);
    TEST_ASSERT_EQUAL_UINT8(90, s_out.solar_planes[0].tilt);
    TEST_ASSERT_EQUAL_INT16(180, s_out.solar_planes[0].azimuth);
    TEST_ASSERT_EQUAL_UINT16(10000, s_out.solar_planes[1].kwp_e2);
    TEST_ASSERT_EQUAL_UINT8(0, s_out.solar_planes[1].tilt);
    TEST_ASSERT_EQUAL_INT16(-180, s_out.solar_planes[1].azimuth);
    TEST_ASSERT_EQUAL_UINT8(50, s_out.solar_losses_pct);
    TEST_ASSERT_EQUAL_UINT16(10000, s_out.solar_inverter_kw_e2);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_OFF, s_out.energy_source);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_AUTO, s_out.energy_battery);
    json = "{\"schema\":1,\"solar\":{\"planes\":[{\"tilt\":10}]}}"; /* a plane's missing values: the default's */
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.solar_plane_count);
    TEST_ASSERT_EQUAL_UINT16(500, s_out.solar_planes[0].kwp_e2);
    TEST_ASSERT_EQUAL_UINT8(10, s_out.solar_planes[0].tilt);
    json = "{\"schema\":1,\"solar\":{\"planes\":[]}}"; /* no plane is the default one */
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.solar_plane_count);
}

/* SolaX's Developer API (D37): its source and region round-trip; an unknown region is the default, the EU. */
static void test_the_solax_developer_api_and_its_region(void)
{
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_REGION_EU, s_defaults.energy_region);
    const char *json = "{\"schema\":1,\"energy\":{\"source\":\"solax-dev\",\"region\":\"cn\",\"battery\":\"auto\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_SOLAX_DEV, s_out.energy_source);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_REGION_CN, s_out.energy_region);
    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"source\":\t\"solax-dev\""));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"region\":\t\"cn\""));
    settings_t again;
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
    json = "{\"schema\":1,\"energy\":{\"region\":\"mars\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_REGION_EU, s_out.energy_region);
}

/* The application's client id and secret are keys like the others: taken out of a PATCH, letters, digits, - and _,
 * 63 at most; setting or clearing one makes the device log in afresh. */
static void test_the_developer_apis_client_id_and_secret_are_keys(void)
{
    settings_secrets_t secrets;
    char clean[256];
    const char *patch = "{\"energy\":{\"source\":\"solax-dev\",\"solax_client_id\":\"c5257b5a04074131a37dd\","
                        "\"solax_client_secret\":\"Ab-U741ebBrQar41aTupqdH29Rq94Il_JDg\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
                             s_err);
    TEST_ASSERT_NULL(strstr(clean, "solax_client"));
    TEST_ASSERT_EQUAL_STRING("c5257b5a04074131a37dd", secrets.value[SETTINGS_SECRET_SOLAX_CLIENT_ID]);
    TEST_ASSERT_EQUAL_STRING("Ab-U741ebBrQar41aTupqdH29Rq94Il_JDg", secrets.value[SETTINGS_SECRET_SOLAX_CLIENT_SECRET]);
    TEST_ASSERT_EQUAL_STRING("solax_client_id", settings_secret_key(SETTINGS_SECRET_SOLAX_CLIENT_ID));
    TEST_ASSERT_EQUAL_STRING("solax_secret", settings_secret_key(SETTINGS_SECRET_SOLAX_CLIENT_SECRET));
    TEST_ASSERT_TRUE(settings_secrets_solax_dev(&secrets));
    settings_secrets_t other = { 0 };
    other.given[SETTINGS_SECRET_SOLAX_TOKEN] = true;
    TEST_ASSERT_FALSE(settings_secrets_solax_dev(&other));
    patch = "{\"energy\":{\"solax_client_secret\":\"a+b\"}}";
    TEST_ASSERT_EQUAL_UINT(0, settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("energy.solax_client_secret: letters, digits, - and _ only", s_err);
}

static void test_secrets_never_reach_the_file(void)
{
    const char *patch = "{\"solar\":{\"source\":\"solcast\",\"solcast_key\":\"Kk_1-2\","
                        "\"solcast_sites\":[\"ab12-cd34\",\"ef56\"],\"fs_key\":\"AbC123\"},"
                        "\"energy\":{\"solax_token\":\"20200722\",\"solax_sn\":\"SXA1B2C3D4\",\"battery\":\"off\","
                        "\"solax_client_id\":\"Cid-1\",\"solax_client_secret\":\"Sec_2\"}}";
    settings_secrets_t secrets;
    char clean[512];
    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
                             s_err);
    TEST_ASSERT_NULL(strstr(clean, "solcast_key"));
    TEST_ASSERT_NULL(strstr(clean, "solax"));
    TEST_ASSERT_NULL(strstr(clean, "fs_key"));
    TEST_ASSERT_NULL(strstr(clean, "Kk_1"));
    TEST_ASSERT_NOT_NULL(strstr(clean, "\"source\":\"solcast\""));
    TEST_ASSERT_NOT_NULL(strstr(clean, "\"battery\":\"off\""));
    for (int i = 0; i < SETTINGS_SECRET_COUNT; i++) {
        TEST_ASSERT_TRUE(secrets.given[i]);
    }
    TEST_ASSERT_EQUAL_STRING("Kk_1-2", secrets.value[SETTINGS_SECRET_SOLCAST_KEY]);
    TEST_ASSERT_EQUAL_STRING("ab12-cd34", secrets.value[SETTINGS_SECRET_SOLCAST_SITE1]);
    TEST_ASSERT_EQUAL_STRING("ef56", secrets.value[SETTINGS_SECRET_SOLCAST_SITE2]);
    TEST_ASSERT_EQUAL_STRING("AbC123", secrets.value[SETTINGS_SECRET_FS_KEY]);
    TEST_ASSERT_EQUAL_STRING("20200722", secrets.value[SETTINGS_SECRET_SOLAX_TOKEN]);
    TEST_ASSERT_EQUAL_STRING("SXA1B2C3D4", secrets.value[SETTINGS_SECRET_SOLAX_SN]);
    TEST_ASSERT_EQUAL_STRING("solcast_site2", settings_secret_key(SETTINGS_SECRET_SOLCAST_SITE2));
    TEST_ASSERT_TRUE(settings_patch("{\"schema\":1}", clean, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
    TEST_ASSERT_NULL(strstr(s_json, "Kk_1"));
}

static void test_a_secret_is_cleared_with_null_or_nothing(void)
{
    settings_secrets_t secrets;
    char clean[256];
    const char *patch = "{\"solar\":{\"fs_key\":null,\"solcast_sites\":[]},\"energy\":{\"solax_token\":\"\"}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
                             s_err);
    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_FS_KEY]);
    TEST_ASSERT_EQUAL_STRING("", secrets.value[SETTINGS_SECRET_FS_KEY]);
    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLCAST_SITE1]);
    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLCAST_SITE2]);
    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLAX_TOKEN]);
    TEST_ASSERT_FALSE(secrets.given[SETTINGS_SECRET_SOLAX_SN]); /* not named: as it is */
    TEST_ASSERT_FALSE(secrets.given[SETTINGS_SECRET_SOLCAST_KEY]);
    TEST_ASSERT_EQUAL_STRING("{\"solar\":{},\"energy\":{}}", clean);
}

/* A key named twice, or in a second "solar" object, is taken out too, the last counting as in a merge; the "keys"
 * flags a GET adds are dropped, so a page that sends them back doesn't store them. */
static void test_no_key_stays_behind(void)
{
    settings_secrets_t secrets;
    char clean[256];
    const char *patch = "{\"solar\":{\"fs_key\":\"Aa1\",\"fs_key\":\"Bb2\",\"keys\":{\"fs_key\":true}},"
                        "\"solar\":{\"solcast_key\":\"Cc3\"},"
                        "\"energy\":{\"keys\":{\"solax_sn\":false}}}";
    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
                             s_err);
    TEST_ASSERT_EQUAL_STRING("{\"solar\":{},\"solar\":{},\"energy\":{}}", clean);
    TEST_ASSERT_EQUAL_STRING("Bb2", secrets.value[SETTINGS_SECRET_FS_KEY]);
    TEST_ASSERT_EQUAL_STRING("Cc3", secrets.value[SETTINGS_SECRET_SOLCAST_KEY]);
}

/* A new Solcast key or site starts its budget over, so the page's Check now asks at once (spec §11.5); other keys
 * don't. */
static void test_a_solcast_key_or_site_starts_its_budget_over(void)
{
    settings_secrets_t secrets = { 0 };
    secrets.given[SETTINGS_SECRET_FS_KEY] = secrets.given[SETTINGS_SECRET_SOLAX_TOKEN] = true;
    TEST_ASSERT_FALSE(settings_secrets_solcast(&secrets));
    for (int i = SETTINGS_SECRET_SOLCAST_KEY; i <= SETTINGS_SECRET_SOLCAST_SITE2; i++) {
        settings_secrets_t one = { 0 };
        one.given[i] = true;
        TEST_ASSERT_TRUE(settings_secrets_solcast(&one));
    }
}

static void test_secrets_the_requests_cannot_carry_are_refused(void)
{
    static const struct {
        const char *patch, *why;
    } k_cases[] = {
        { "{\"solar\":{\"fs_key\":\"ab/c\"}}", "solar.fs_key: letters and digits only" },
        { "{\"solar\":{\"solcast_key\":\"a b\"}}", "solar.solcast_key: letters, digits, - and _ only" },
        { "{\"solar\":{\"solcast_sites\":[\"ab12?x\"]}}", "solar.solcast_sites: letters, digits and - only" },
        { "{\"solar\":{\"solcast_sites\":[\"a\",\"b\",\"c\"]}}", "solar.solcast_sites: two at most" },
        { "{\"solar\":{\"solcast_sites\":\"a\"}}", "solar.solcast_sites: a list" },
        { "{\"energy\":{\"solax_token\":\"20\\r\\n20\"}}", "energy.solax_token: letters and digits only" },
        { "{\"energy\":{\"solax_sn\":42}}", "energy.solax_sn: a string" },
        { "{\"energy\":{\"solax_sn\":\"0123456789012345678901234567890123456789012345678901234567890123\"}}",
          "energy.solax_sn: 63 characters at most" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        settings_secrets_t secrets;
        char clean[256];
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, settings_take_secrets(k_cases[i].patch, clean, sizeof(clean), &secrets,
                                                                s_err, sizeof(s_err)),
                                       k_cases[i].patch);
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

static void test_a_second_plane_needs_a_forecast_solar_key(void)
{
    settings_t s = s_defaults;
    s.solar_source = SETTINGS_SOLAR_FORECAST_SOLAR;
    s.solar_plane_count = 2;
    TEST_ASSERT_FALSE(settings_check_solar(&s, false, s_err, sizeof(s_err))); /* spec §11.5 */
    TEST_ASSERT_EQUAL_STRING("a second plane needs a Forecast.Solar key", s_err);
    TEST_ASSERT_TRUE(settings_check_solar(&s, true, s_err, sizeof(s_err)));
    s.solar_source = SETTINGS_SOLAR_OPEN_METEO; /* our model takes both */
    TEST_ASSERT_TRUE(settings_check_solar(&s, false, s_err, sizeof(s_err)));
}

static void test_the_largest_settings_fit_the_file_buffer(void)
{
    settings_t s = s_defaults;
    memset(s.place, 'W', sizeof(s.place) - 1);
    s.place[sizeof(s.place) - 1] = '\0';
    memset(s.tz_iana, 'Z', sizeof(s.tz_iana) - 1);
    s.tz_iana[sizeof(s.tz_iana) - 1] = '\0';
    memset(s.tz_posix, 'P', sizeof(s.tz_posix) - 1);
    s.tz_posix[sizeof(s.tz_posix) - 1] = '\0';
    for (int i = 0; i < SETTINGS_NTP_MAX; i++) {
        memset(s.ntp[i], 'n', SETTINGS_HOST_LEN - 1);
        s.ntp[i][SETTINGS_HOST_LEN - 1] = '\0';
    }
    s.sync_time_count = SETTINGS_SYNC_TIMES_MAX;
    for (int i = 0; i < SETTINGS_SYNC_TIMES_MAX; i++) {
        s.sync_times[i] = (uint16_t)(1300 + i * 17);
    }
    for (int i = 0; i < SETTINGS_BAT_CURVE_POINTS; i++) {
        s.bat_learned_mv[i] = (uint16_t)(3333 + i);
    }
    s.bat_learned_at = UINT32_MAX;
    s.temp_offset_c100 = -999;
    s.hum_offset_pct100 = -1999;
    s.wx_centre_set = s.fl_centre_set = true;
    s.wx_lat_e4 = s.fl_lat_e4 = -849999;
    s.wx_lon_e4 = s.fl_lon_e4 = -1799999;
    s.solar_plane_count = 2;
    for (int i = 0; i < 2; i++) {
        s.solar_planes[i] = (settings_plane_t){ .kwp_e2 = 9999, .tilt = 89, .azimuth = -179 };
    }
    s.solar_source = SETTINGS_SOLAR_FORECAST_SOLAR;
    s.solar_inverter_kw_e2 = 9999;
    size_t n = settings_to_json(&s, NULL, s_json, sizeof(s_json));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < SETTINGS_FILE_MAX / 2); /* room for keys a later firmware adds (M7's mqtt.*) */
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
    RUN_TEST(test_a_learned_battery_curve_parses_and_round_trips);
    RUN_TEST(test_learned_without_a_usable_curve_keeps_the_built_in_one);
    RUN_TEST(test_the_sync_settings_parse_and_round_trip);
    RUN_TEST(test_the_mode_before_always_parses_and_round_trips);
    RUN_TEST(test_boot_double_toggles_always_and_back);
    RUN_TEST(test_entering_always_remembers_the_mode_it_left);
    RUN_TEST(test_settings_that_replace_others_remember_the_mode_left);
    RUN_TEST(test_sync_times_are_sorted_without_repeats_or_bad_ones);
    RUN_TEST(test_sync_settings_fall_back_one_by_one);
    RUN_TEST(test_the_sync_defaults_are_the_specs);
    RUN_TEST(test_the_radars_default_to_the_location);
    RUN_TEST(test_the_radar_settings_parse_clamp_and_round_trip);
    RUN_TEST(test_a_centre_that_follows_the_location_is_not_saved);
    RUN_TEST(test_the_solar_defaults_are_the_specs);
    RUN_TEST(test_the_sync_steps_parse_and_round_trip);
    RUN_TEST(test_the_solar_settings_parse_clamp_and_round_trip);
    RUN_TEST(test_the_solax_developer_api_and_its_region);
    RUN_TEST(test_the_developer_apis_client_id_and_secret_are_keys);
    RUN_TEST(test_secrets_never_reach_the_file);
    RUN_TEST(test_a_secret_is_cleared_with_null_or_nothing);
    RUN_TEST(test_no_key_stays_behind);
    RUN_TEST(test_a_solcast_key_or_site_starts_its_budget_over);
    RUN_TEST(test_secrets_the_requests_cannot_carry_are_refused);
    RUN_TEST(test_a_second_plane_needs_a_forecast_solar_key);
    RUN_TEST(test_the_largest_settings_fit_the_file_buffer);
    return UNITY_END();
}
