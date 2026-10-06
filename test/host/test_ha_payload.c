#define _POSIX_C_SOURCE 200809L /* setenv, tzset */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "ha_payload.h"
#include "unity.h"

static char s_topic[HA_TOPIC_MAX], s_payload[HA_PAYLOAD_MAX];
static ha_value_t s_v;

void setUp(void)
{
    memset(&s_v, 0xAA, sizeof(s_v));
}

void tearDown(void) {}

static void test_topics_are_under_the_device_id(void)
{
    ha_topic(s_topic, sizeof(s_topic), "reflbo-bb94", "state");
    TEST_ASSERT_EQUAL_STRING("reflbo/reflbo-bb94/state", s_topic);
    ha_topic(s_topic, sizeof(s_topic), "reflbo-bb94", "cmd/#");
    TEST_ASSERT_EQUAL_STRING("reflbo/reflbo-bb94/cmd/#", s_topic);
}

/* spec §12.4: reflbo/<id>/cmd/<name>; anything else is no command. */
static void test_commands_come_by_their_topic(void)
{
    static const struct {
        const char *topic;
        ha_cmd_t cmd;
    } k_cases[] = {
        { "reflbo/reflbo-bb94/cmd/preset", HA_CMD_PRESET }, { "reflbo/reflbo-bb94/cmd/next", HA_CMD_NEXT },
        { "reflbo/reflbo-bb94/cmd/sync", HA_CMD_SYNC },     { "reflbo/reflbo-bb94/cmd/message", HA_CMD_MESSAGE },
        { "reflbo/reflbo-bb94/cmd/reboot", HA_CMD_NONE },   { "reflbo/reflbo-aaaa/cmd/next", HA_CMD_NONE },
        { "reflbo/reflbo-bb94/cmd/next/x", HA_CMD_NONE },   { "zigbee2mqtt/x", HA_CMD_NONE },
        { "reflbo/reflbo-bb94/cmd/", HA_CMD_NONE },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        const char *topic = k_cases[i].topic;
        TEST_ASSERT_EQUAL_MESSAGE(k_cases[i].cmd, ha_cmd_parse("reflbo-bb94", topic, strlen(topic)), topic);
    }
    /* esp-mqtt's topics aren't NUL-terminated: only `len` bytes count */
    TEST_ASSERT_EQUAL(HA_CMD_NEXT, ha_cmd_parse("reflbo-bb94", "reflbo/reflbo-bb94/cmd/nextXYZ", 27));
    TEST_ASSERT_TRUE(ha_cmd_press("PRESS", 5));
    TEST_ASSERT_FALSE(ha_cmd_press("PRESSED", 7));
    TEST_ASSERT_FALSE(ha_cmd_press("press", 5));
}

/* spec §12.7: up to 96 bytes, cut at a character; control characters as spaces. */
static void test_a_message_is_cut_at_a_character(void)
{
    char text[HA_MESSAGE_LEN];
    ha_message_text("Washing\nmachine done", 20, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Washing machine done", text);
    char long_text[128];
    memset(long_text, 'x', 95);
    snprintf(long_text + 95, sizeof(long_text) - 95, "%s", "čxyz"); /* "č" at bytes 95-96 */
    ha_message_text(long_text, strlen(long_text), text, sizeof(text));
    TEST_ASSERT_EQUAL_UINT(95, strlen(text));
    ha_message_text("", 0, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
}

static ha_field_t field(ha_kind_t kind, uint8_t precision, const char *path)
{
    ha_field_t f = { .key = "k", .label = "K", .kind = (uint8_t)kind, .precision = precision, .topic = "t" };
    snprintf(f.json_path, sizeof(f.json_path), "%s", path);
    return f;
}

static bool parse(ha_field_t f, const char *payload)
{
    return ha_value_parse(&f, payload, strlen(payload), &s_v);
}

/* spec §12.5: the payload, or the json_path's value in it, as a number or a text. */
static void test_numbers_keep_their_decimals_or_the_precision(void)
{
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "21.53"));
    TEST_ASSERT_EQUAL_INT32(2153, s_v.number);
    TEST_ASSERT_EQUAL_UINT8(2, s_v.decimals);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 1, ""), " 21.56\n"));
    TEST_ASSERT_EQUAL_INT32(216, s_v.number);
    TEST_ASSERT_EQUAL_UINT8(1, s_v.decimals);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, ""), "-3.5"));
    TEST_ASSERT_EQUAL_INT32(-4, s_v.number); /* half away from zero */
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "612"));
    TEST_ASSERT_EQUAL_INT32(612, s_v.number);
    TEST_ASSERT_EQUAL_UINT8(0, s_v.decimals);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "3.14159"));
    TEST_ASSERT_EQUAL_INT32(3142, s_v.number); /* 3 decimals at most */
    TEST_ASSERT_EQUAL_UINT8(3, s_v.decimals);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 3, ""), "1234567890"));
    TEST_ASSERT_EQUAL_INT32(1234567890, s_v.number); /* too large for 3 decimals: fewer */
    TEST_ASSERT_EQUAL_UINT8(0, s_v.decimals);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "\"7.5\""));
    TEST_ASSERT_EQUAL_INT32(75, s_v.number);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "true"));
    TEST_ASSERT_EQUAL_INT32(1, s_v.number);
    static const char *const k_not_numbers[] = { "Unavailable", "21.5 °C", "nan", "1e30", "{\"a\": 1}" };
    for (size_t i = 0; i < sizeof(k_not_numbers) / sizeof(k_not_numbers[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), k_not_numbers[i]),
                                  k_not_numbers[i]);
    }
}

static void test_texts_are_cut_at_a_character(void)
{
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "Partly cloudy"));
    TEST_ASSERT_EQUAL_STRING("Partly cloudy", s_v.text);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "\"quoted\""));
    TEST_ASSERT_EQUAL_STRING("quoted", s_v.text); /* a JSON string's value */
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "21.5"));
    TEST_ASSERT_EQUAL_STRING("21.5", s_v.text);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "false"));
    TEST_ASSERT_EQUAL_STRING("off", s_v.text);
    char long_text[64];
    memset(long_text, 'x', 46);
    snprintf(long_text + 46, sizeof(long_text) - 46, "%s", "ěend"); /* "ě" at bytes 46-47 */
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), long_text));
    TEST_ASSERT_EQUAL_UINT(46, strlen(s_v.text));
    TEST_ASSERT_FALSE(s_v.none);
}

/* HA's unknown and unavailable, an empty payload and JSON's null are no value for every kind (D40): the field
 * clears, where a value that doesn't read leaves it as it was. */
static void test_ha_says_no_value(void)
{
    static const char *const k_none[] = { "unknown", "unavailable", "", "  \n", "null", "\"unavailable\"" };
    static const ha_kind_t k_kinds[] = { HA_KIND_NUMBER, HA_KIND_TEXT, HA_KIND_TIME };
    for (size_t k = 0; k < 3; k++) {
        for (size_t i = 0; i < sizeof(k_none) / sizeof(k_none[0]); i++) {
            TEST_ASSERT_TRUE_MESSAGE(parse(field(k_kinds[k], 0, ""), k_none[i]), k_none[i]);
            TEST_ASSERT_TRUE_MESSAGE(s_v.none, k_none[i]);
        }
    }
    const char *z2m = "{\"temperature\": null, \"state\": \"unknown\", \"co2\": 600}";
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, "temperature"), z2m));
    TEST_ASSERT_TRUE(s_v.none);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "state"), z2m));
    TEST_ASSERT_TRUE(s_v.none);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, "co2"), z2m));
    TEST_ASSERT_FALSE(s_v.none);
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, ""), "Unknown")); /* HA's words are lower case: not a number */
}

/* A text field's state labels (D40) apply as the value comes, so what the store keeps is what shows. */
static void test_a_state_shows_as_its_label(void)
{
    ha_field_t f = field(HA_KIND_TEXT, 0, "");
    f.state_count = 2;
    f.states[0] = (ha_state_label_t){ .state = "on", .label = "Open" };
    f.states[1] = (ha_state_label_t){ .state = "not_home", .label = "Pryč" };
    TEST_ASSERT_TRUE(parse(f, "on"));
    TEST_ASSERT_EQUAL_STRING("Open", s_v.text);
    TEST_ASSERT_TRUE(parse(f, "\"not_home\""));
    TEST_ASSERT_EQUAL_STRING("Pryč", s_v.text);
    TEST_ASSERT_TRUE(parse(f, "true")); /* a boolean reads "on" first */
    TEST_ASSERT_EQUAL_STRING("Open", s_v.text);
    TEST_ASSERT_TRUE(parse(f, "home"));
    TEST_ASSERT_EQUAL_STRING("home", s_v.text); /* no label: as it came */
    f.state_count = 0;
    TEST_ASSERT_TRUE(parse(f, "on"));
    TEST_ASSERT_EQUAL_STRING("on", s_v.text);
}

/* A text payload that looks like a number stays as it came, as HA's statestream sends every state as text: a
 * firmware's "1.10" or a code's "0123" isn't the number they would print as, and a state label may name it. A JSON
 * number at a json_path is a number, and reads as one. */
static void test_a_text_keeps_a_number_as_it_came(void)
{
    ha_field_t f = field(HA_KIND_TEXT, 0, "");
    static const char *const k_texts[] = { "2.0", "0123", "1e5", "-0", "1.10" };
    for (size_t i = 0; i < sizeof(k_texts) / sizeof(k_texts[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(parse(f, k_texts[i]), k_texts[i]);
        TEST_ASSERT_EQUAL_STRING(k_texts[i], s_v.text);
    }
    f.state_count = 1;
    f.states[0] = (ha_state_label_t){ .state = "2.0", .label = "Eco" };
    TEST_ASSERT_TRUE(parse(f, "2.0"));
    TEST_ASSERT_EQUAL_STRING("Eco", s_v.text);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "v"), "{\"v\": 2.0}"));
    TEST_ASSERT_EQUAL_STRING("2", s_v.text);
}

/* A boolean (Zigbee2MQTT's contact or occupancy) takes a label for its own word first, true or false, then for on or
 * off, and else reads on or off; a label applies once. Zigbee2MQTT's contact is true while a door is closed. */
static void test_a_boolean_takes_its_own_label_first(void)
{
    ha_field_t f = field(HA_KIND_TEXT, 0, "contact");
    f.state_count = 3;
    f.states[0] = (ha_state_label_t){ .state = "true", .label = "Closed" };
    f.states[1] = (ha_state_label_t){ .state = "on", .label = "Open" };
    f.states[2] = (ha_state_label_t){ .state = "Closed", .label = "Zav\xC5\x99" "eno" };
    TEST_ASSERT_TRUE(parse(f, "{\"contact\": true}"));
    TEST_ASSERT_EQUAL_STRING("Closed", s_v.text);
    TEST_ASSERT_TRUE(parse(f, "{\"contact\": false}"));
    TEST_ASSERT_EQUAL_STRING("off", s_v.text); /* neither false nor off has a label */
    f.state_count = 2;
    f.states[0] = (ha_state_label_t){ .state = "false", .label = "Open" };
    TEST_ASSERT_TRUE(parse(f, "{\"contact\": false}"));
    TEST_ASSERT_EQUAL_STRING("Open", s_v.text);
    TEST_ASSERT_TRUE(parse(f, "{\"contact\": true}"));
    TEST_ASSERT_EQUAL_STRING("Open", s_v.text); /* on's label, as true has none */
}

/* A date alone, as HA's date sensors have it (the bins' next collection, a birthday): its local midnight, marked as a
 * date, so it shows as one (D40). A date that doesn't exist, or another form, doesn't read. */
static void test_a_date_alone_is_its_local_midnight(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TIME, 0, ""), "2026-10-12"));
    TEST_ASSERT_TRUE(s_v.date_only);
    TEST_ASSERT_EQUAL_UINT32(1791756000, s_v.time); /* 2026-10-11 22:00 UTC */
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TIME, 0, "d"), "{\"d\": \"2026-10-12\"}"));
    TEST_ASSERT_TRUE(s_v.date_only);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TIME, 0, ""), "2026-10-06T12:30:00Z"));
    TEST_ASSERT_FALSE(s_v.date_only);
    static const char *const k_bad[] = { "2026-02-30", "2026-1-12", "2026-10-12x", "12.10.2026", "1969-12-31" };
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(parse(field(HA_KIND_TIME, 0, ""), k_bad[i]), k_bad[i]);
    }
}

/* A time (D40): ISO 8601 with its zone, a fraction of a second dropped, or seconds or ms since 1970 from 2001 on;
 * anything else doesn't read, and the field keeps what it had. */
static void test_times_read_iso_8601_or_seconds(void)
{
    static const struct {
        const char *payload;
        uint32_t utc;
    } k_cases[] = {
        { "2026-10-06T12:30:00+00:00", 1791289800 },     { "\"2026-10-06T12:30:00.123456+00:00\"", 1791289800 },
        { "2026-10-06T14:30:00+02:00", 1791289800 },     { "2026-10-06T12:30:00Z", 1791289800 },
        { "1791289800", 1791289800 },                    { "1791289800000", 1791289800 },
        { "\"1791282600\"", 1791282600 },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(parse(field(HA_KIND_TIME, 0, ""), k_cases[i].payload), k_cases[i].payload);
        TEST_ASSERT_FALSE(s_v.none);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(k_cases[i].utc, s_v.time, k_cases[i].payload);
    }
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TIME, 0, "next.at"), "{\"next\": {\"at\": \"2026-10-06T12:30:00+00:00\"}}"));
    TEST_ASSERT_EQUAL_UINT32(1791289800, s_v.time);
    static const char *const k_bad[] = { "soon", "20261006", "2026-13-06T12:30:00Z", "true", "{\"a\": 1}" };
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(parse(field(HA_KIND_TIME, 0, ""), k_bad[i]), k_bad[i]);
    }
}

/* Zigbee2MQTT and HA's statestream attributes: JSON objects, read by dotted keys. */
static void test_a_json_path_picks_the_value(void)
{
    const char *z2m = "{\"co2\": 612, \"update\": {\"state\": \"idle\", \"installed\": 2.5}, \"contact\": true}";
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, "co2"), z2m));
    TEST_ASSERT_EQUAL_INT32(612, s_v.number);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "update.state"), z2m));
    TEST_ASSERT_EQUAL_STRING("idle", s_v.text);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, "update.installed"), z2m));
    TEST_ASSERT_EQUAL_INT32(25, s_v.number);
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "contact"), z2m));
    TEST_ASSERT_EQUAL_STRING("on", s_v.text);
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "voc"), z2m));
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "update"), z2m)); /* an object */
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2.x"), z2m));
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2"), "not json"));
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2"), "[612]"));
}

/* A payload nested past the cap is refused unparsed (AGENTS.md gotcha 30). */
static void test_deep_payloads_are_refused(void)
{
    char deep[256];
    size_t n = 0;
    for (int i = 0; i < 40; i++) {
        deep[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        deep[n++] = ']';
    }
    deep[n] = '\0';
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "a"), deep));
    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, ""), deep));
    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), deep)); /* a text field shows it as it came, cut */
    TEST_ASSERT_EQUAL_UINT(HA_TEXT_LEN - 1, strlen(s_v.text));
}

static ha_state_t sample_state(void)
{
    return (ha_state_t){ .has_temp = true, .temp_c100 = 2140, .has_hum = true, .hum_pct100 = 4520, .has_battery = true,
                         .bat_pct = 78, .bat_mv = 3921, .charging = "discharging", .has_rssi = true, .rssi = -61,
                         .preset_id = "home", .preset_name = "Home", .last_sync = 1790307012, .fw = "0.1.0",
                         .uptime_s = 86400 };
}

/* spec §12.2's example, and the preset's name for the select. */
static void test_the_state_json(void)
{
    ha_state_t st = sample_state();
    TEST_ASSERT_TRUE(ha_state_json(&st, s_payload, sizeof(s_payload)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":21.4,\"hum\":45.2,\"bat_pct\":78,\"bat_v\":3.92,\"charging\":\"discharging\","
                             "\"rssi\":-61,\"preset\":\"home\",\"preset_name\":\"Home\","
                             "\"last_sync\":\"2026-09-25T03:30:12Z\",\"fw\":\"0.1.0\",\"uptime_s\":86400}",
                             s_payload);
    st = (ha_state_t){ .charging = "unknown", .preset_id = "home", .preset_name = "Home", .fw = "0.1.0" };
    TEST_ASSERT_TRUE(ha_state_json(&st, s_payload, sizeof(s_payload)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":null,\"hum\":null,\"bat_pct\":null,\"bat_v\":null,\"charging\":\"unknown\","
                             "\"rssi\":null,\"preset\":\"home\",\"preset_name\":\"Home\",\"last_sync\":null,"
                             "\"fw\":\"0.1.0\",\"uptime_s\":0}",
                             s_payload);
    TEST_ASSERT_EQUAL_UINT(0, ha_state_json(&st, s_payload, 16));
}

/* spec §12.3: twice the expected interval plus 10 min; none without one (manual mode). */
static void test_expire_after(void)
{
    TEST_ASSERT_EQUAL_UINT32(2 * 86400 + 600, ha_expire_after_s(86400));
    TEST_ASSERT_EQUAL_UINT32(1800, ha_expire_after_s(600));
    TEST_ASSERT_EQUAL_UINT32(0, ha_expire_after_s(0));
}

static const char *const k_presets[] = { "Home", "Indoor", "Weather", "Focus clock", "Rain radar", "Flights" };

static ha_disc_t discovery(void)
{
    return (ha_disc_t){ .id = "reflbo-bb94", .prefix = "homeassistant", .fw = "0.1.0", .expire_after_s = 173400,
                        .presets = k_presets, .preset_count = 6, .broker = "192.168.1.10:1883" };
}

/* Golden JSON for every entity (spec §17): test/host/fixtures/ha/discovery.txt holds each topic and payload
 * on two lines, a blank line after each. */
static void test_discovery_matches_its_golden(void)
{
    static char golden[32768], made[32768];
    FILE *f = fopen(FIXTURE_DIR "/discovery.txt", "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, FIXTURE_DIR "/discovery.txt");
    size_t n = fread(golden, 1, sizeof(golden) - 1, f);
    fclose(f);
    golden[n] = '\0';
    ha_disc_t d = discovery();
    size_t at = 0;
    for (int i = 0; i < HA_DISC_COUNT; i++) {
        TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
        at += (size_t)snprintf(made + at, sizeof(made) - at, "%s\n%s\n\n", s_topic, s_payload);
        cJSON *json = cJSON_Parse(s_payload);
        TEST_ASSERT_NOT_NULL_MESSAGE(json, s_topic);
        cJSON_Delete(json);
    }
    if (strcmp(golden, made) != 0) {
        FILE *out = fopen("discovery.actual.txt", "wb");
        if (out != NULL) {
            fputs(made, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_STRING(golden, made);
    TEST_ASSERT_FALSE(ha_disc_message(&d, HA_DISC_COUNT, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
}

/* Discovery goes out again when its hash changes (spec §12.3): the preset names, the expected interval, the
 * prefix, the firmware, or the broker it goes to. */
static void test_the_hash_follows_what_discovery_says(void)
{
    ha_disc_t d = discovery();
    uint32_t h = ha_disc_hash(&d);
    TEST_ASSERT_EQUAL_UINT32(h, ha_disc_hash(&d));
    const char *renamed[] = { "Home", "Indoor", "Weather", "Focus", "Rain radar", "Flights" };
    ha_disc_t e = d;
    e.presets = renamed;
    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
    e = d;
    e.expire_after_s = 0;
    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
    e = d;
    e.prefix = "ha";
    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
    e = d;
    e.fw = "0.2.0";
    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
    e = d;
    e.broker = "homeassistant.local:1883"; /* another broker hasn't seen them */
    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
}

/* The largest discovery message: 16 presets with the longest names in the select, of control characters,
 * which JSON writes in 6 bytes each ("\u0001"). Every config fits. */
static void test_the_largest_discovery_message_fits(void)
{
    static char names[16][24];
    const char *list[16];
    for (int i = 0; i < 16; i++) {
        memset(names[i], 0x01, 22);
        names[i][22] = (char)('a' + i);
        list[i] = names[i];
    }
    ha_disc_t d = discovery();
    d.presets = list;
    d.preset_count = 16;
    d.prefix = "a234567890123456789012345678901"; /* 31 bytes */
    int largest = 0;
    for (int i = 0; i < HA_DISC_COUNT; i++) {
        TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
        int len = (int)strlen(s_payload);
        largest = len > largest ? len : largest;
    }
    printf("the largest discovery payload: %d bytes of %d\n", largest, HA_PAYLOAD_MAX);
}

/* spec §12.8: each dashboard gesture's payload on reflbo/<id>/action, which a device trigger of discovery
 * waits for. */
static void test_key_presses_have_their_payloads(void)
{
    TEST_ASSERT_EQUAL_STRING("key_short", ha_action_payload(false, HA_PRESS_SHORT));
    TEST_ASSERT_EQUAL_STRING("key_double", ha_action_payload(false, HA_PRESS_DOUBLE));
    TEST_ASSERT_EQUAL_STRING("key_long", ha_action_payload(false, HA_PRESS_LONG));
    TEST_ASSERT_EQUAL_STRING("boot_short", ha_action_payload(true, HA_PRESS_SHORT));
    TEST_ASSERT_EQUAL_STRING("boot_double", ha_action_payload(true, HA_PRESS_DOUBLE));
    TEST_ASSERT_EQUAL_STRING("boot_long", ha_action_payload(true, HA_PRESS_LONG));
    TEST_ASSERT_NULL(ha_action_payload(false, (ha_press_t)3));
    ha_disc_t d = discovery();
    for (int boot = 0; boot < 2; boot++) {
        for (int p = HA_PRESS_SHORT; p <= HA_PRESS_LONG; p++) {
            char want[48];
            snprintf(want, sizeof(want), "\"payload\":\"%s\"", ha_action_payload(boot != 0, (ha_press_t)p));
            bool found = false;
            for (int i = 0; i < HA_DISC_COUNT && !found; i++) {
                TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
                found = strstr(s_payload, want) != NULL;
            }
            TEST_ASSERT_TRUE_MESSAGE(found, want);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_topics_are_under_the_device_id);
    RUN_TEST(test_commands_come_by_their_topic);
    RUN_TEST(test_a_message_is_cut_at_a_character);
    RUN_TEST(test_numbers_keep_their_decimals_or_the_precision);
    RUN_TEST(test_texts_are_cut_at_a_character);
    RUN_TEST(test_ha_says_no_value);
    RUN_TEST(test_a_state_shows_as_its_label);
    RUN_TEST(test_times_read_iso_8601_or_seconds);
    RUN_TEST(test_a_json_path_picks_the_value);
    RUN_TEST(test_deep_payloads_are_refused);
    RUN_TEST(test_the_state_json);
    RUN_TEST(test_expire_after);
    RUN_TEST(test_discovery_matches_its_golden);
    RUN_TEST(test_the_hash_follows_what_discovery_says);
    RUN_TEST(test_the_largest_discovery_message_fits);
    RUN_TEST(test_key_presses_have_their_payloads);
    RUN_TEST(test_a_text_keeps_a_number_as_it_came);
    RUN_TEST(test_a_boolean_takes_its_own_label_first);
    RUN_TEST(test_a_date_alone_is_its_local_midnight);
    return UNITY_END();
}
