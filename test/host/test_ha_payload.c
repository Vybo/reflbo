#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        TEST_ASSERT_EQUAL_MESSAGE(k_cases[i].cmd, ha_cmd_parse("reflbo-bb94", k_cases[i].topic, strlen(k_cases[i].topic)),
                                  k_cases[i].topic);
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
    static const char *const k_not_numbers[] = { "unavailable", "", "21.5 °C", "nan", "1e30", "{\"a\": 1}" };
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
    TEST_ASSERT_FALSE(parse(field(HA_KIND_TEXT, 0, ""), ""));
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_topics_are_under_the_device_id);
    RUN_TEST(test_commands_come_by_their_topic);
    RUN_TEST(test_a_message_is_cut_at_a_character);
    RUN_TEST(test_numbers_keep_their_decimals_or_the_precision);
    RUN_TEST(test_texts_are_cut_at_a_character);
    RUN_TEST(test_a_json_path_picks_the_value);
    RUN_TEST(test_deep_payloads_are_refused);
    RUN_TEST(test_the_state_json);
    RUN_TEST(test_expire_after);
    RUN_TEST(test_discovery_matches_its_golden);
    RUN_TEST(test_the_hash_follows_what_discovery_says);
    RUN_TEST(test_the_largest_discovery_message_fits);
    return UNITY_END();
}
