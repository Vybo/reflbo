#include <stdio.h>
#include <string.h>

#include "ha_fields.h"
#include "unity.h"

static ha_fields_t s_out;
static char s_err[96];
static char s_json[HA_FIELDS_JSON_MAX];

void setUp(void)
{
    memset(&s_out, 0xAA, sizeof(s_out));
    s_err[0] = '\0';
}

void tearDown(void) {}

/* The spec's example (§12.5). */
static const char *k_example =
    "{\"schema\": 1, \"fields\": ["
    " {\"key\": \"outdoor_temp\", \"label\": \"Outside\", \"kind\": \"number\", \"unit\": \"°C\", \"precision\": 1,"
    "  \"topic\": \"ha/statestream/sensor/outdoor_temperature/state\", \"json_path\": null, \"ttl_s\": 172800},"
    " {\"key\": \"co2\", \"label\": \"CO2\", \"kind\": \"number\", \"unit\": \"ppm\", \"precision\": 0,"
    "  \"topic\": \"zigbee2mqtt/living_room\", \"json_path\": \"co2\"}]}";

static void test_the_spec_example_parses(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(k_example, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(2, s_out.count);
    const ha_field_t *f = &s_out.field[0];
    TEST_ASSERT_EQUAL_STRING("outdoor_temp", f->key);
    TEST_ASSERT_EQUAL_STRING("Outside", f->label);
    TEST_ASSERT_EQUAL_UINT8(HA_KIND_NUMBER, f->kind);
    TEST_ASSERT_EQUAL_STRING("°C", f->unit);
    TEST_ASSERT_EQUAL_UINT8(1, f->precision);
    TEST_ASSERT_EQUAL_STRING("ha/statestream/sensor/outdoor_temperature/state", f->topic);
    TEST_ASSERT_EQUAL_STRING("", f->json_path);
    TEST_ASSERT_EQUAL_UINT32(172800, f->ttl_s);
    f = &s_out.field[1];
    TEST_ASSERT_EQUAL_STRING("co2", f->key);
    TEST_ASSERT_EQUAL_UINT8(0, f->precision);
    TEST_ASSERT_EQUAL_STRING("co2", f->json_path);
    TEST_ASSERT_EQUAL_UINT32(0, f->ttl_s); /* the default: twice the expected interval */
    TEST_ASSERT_EQUAL_INT(1, ha_fields_find(&s_out, "co2"));
    TEST_ASSERT_EQUAL_INT(-1, ha_fields_find(&s_out, "co"));
}

/* Only the key and the topic are needed; the rest takes its defaults. */
static void test_a_field_needs_only_a_key_and_a_topic(void)
{
    const char *json = "{\"schema\": 1, \"fields\": [{\"key\": \"door\", \"topic\": \"z2m/door\", \"kind\": \"text\"},"
                       " {\"key\": \"power\", \"topic\": \"z2m/plug\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(json, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("door", s_out.field[0].label); /* the key stands in */
    TEST_ASSERT_EQUAL_UINT8(HA_KIND_TEXT, s_out.field[0].kind);
    TEST_ASSERT_EQUAL_UINT8(HA_KIND_NUMBER, s_out.field[1].kind);
    TEST_ASSERT_EQUAL_UINT8(HA_PRECISION_AUTO, s_out.field[1].precision); /* the payload's own decimals */
    TEST_ASSERT_EQUAL_STRING("", s_out.field[1].unit);
    TEST_ASSERT_TRUE(ha_fields_from_json("{\"schema\": 1}", &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT8(0, s_out.count); /* no mappings yet */
}

static void test_it_round_trips(void)
{
    TEST_ASSERT_TRUE(ha_fields_from_json(k_example, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(ha_fields_to_json(&s_out, s_json, sizeof(s_json)) > 0);
    ha_fields_t again;
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(s_json, &again, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(s_out.count, again.count);
    TEST_ASSERT_EQUAL_MEMORY(s_out.field, again.field, sizeof(again.field[0]) * again.count);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"json_path\":null"), s_json); /* the whole payload */
    TEST_ASSERT_EQUAL_UINT(0, ha_fields_to_json(&s_out, s_json, 16)); /* no room */
}

/* Keys go into presets as mqtt.<key> (spec §12.5): 1-23 bytes of a-z, 0-9 and _. */
static void test_keys_are_short_and_plain(void)
{
    TEST_ASSERT_TRUE(ha_key_valid("outdoor_temp"));
    TEST_ASSERT_TRUE(ha_key_valid("a2345678901234567890123"));
    TEST_ASSERT_FALSE(ha_key_valid("a23456789012345678901234"));
    TEST_ASSERT_FALSE(ha_key_valid(""));
    TEST_ASSERT_FALSE(ha_key_valid("Outdoor"));
    TEST_ASSERT_FALSE(ha_key_valid("out-door"));
    TEST_ASSERT_FALSE(ha_key_valid("out.door"));
    TEST_ASSERT_FALSE(ha_key_valid(NULL));
}

/* A file with a structural error is refused whole, and the error names it. */
static void test_structural_errors_refuse_the_file(void)
{
    static const struct {
        const char *json, *err;
    } k_cases[] = {
        { "{", "not valid JSON" },
        { "{\"schema\": 2}", "schema must be 1" },
        { "{\"schema\": 1, \"fields\": {}}", "fields must be a list" },
        { "{\"schema\": 1, \"fields\": [5]}", "field 1 must be an object" },
        { "{\"schema\": 1, \"fields\": [{\"topic\": \"a\"}]}", "field 1: a key is 1-23 characters of a-z, 0-9 or _" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\"}, {\"key\": \"a\", \"topic\": \"y\"}]}",
          "duplicate key \"a\"" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\"}]}", "field a: a topic of 1-127 printable characters" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m/+/x\"}]}",
          "field a: the topic has a wildcard" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m/#\"}]}", "field a: the topic has a wildcard" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m\\\"x\"}]}",
          "field a: a topic of 1-127 printable characters" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"kind\": \"bool\"}]}",
          "field a: kind is number or text" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"json_path\": \"a..b\"}]}",
          "field a: json_path is keys joined by dots" },
        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"json_path\": 5}]}",
          "field a: json_path is keys joined by dots" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(ha_fields_from_json(k_cases[i].json, &s_out, s_err, sizeof(s_err)), k_cases[i].json);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(k_cases[i].err, s_err, k_cases[i].json);
    }
}

/* `n` fields k0, k1, ... in s_json. */
static const char *many(int n)
{
    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"fields\": [");
    for (int i = 0; i < n; i++) {
        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s{\"key\": \"k%d\", \"topic\": \"t/%d\"}",
                               i ? "," : "", i, i);
    }
    snprintf(s_json + at, sizeof(s_json) - at, "]}");
    return s_json;
}

static void test_at_most_32_fields(void)
{
    TEST_ASSERT_FALSE(ha_fields_from_json(many(HA_FIELDS_MAX + 1), &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("at most 32 MQTT fields", s_err);
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(many(HA_FIELDS_MAX), &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(32, s_out.count);
}

/* Everything else is lenient: labels and units are cut at a character, numbers clamped. */
static void test_the_rest_is_cut_or_clamped(void)
{
    const char *json = "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\","
                       " \"label\": \"abcdefghijklmnopqrstuvč\", \"unit\": \"µg/m³ ok\", \"precision\": 7,"
                       " \"ttl_s\": 5}, {\"key\": \"b\", \"topic\": \"y\", \"label\": \"one\\ntwo\", \"precision\": -1,"
                       " \"ttl_s\": 99999999}]}";
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(json, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuv", s_out.field[0].label); /* 22 bytes: "č" would split */
    TEST_ASSERT_EQUAL_STRING("µg/m³", s_out.field[0].unit);                 /* 7 bytes */
    TEST_ASSERT_EQUAL_UINT8(3, s_out.field[0].precision);
    TEST_ASSERT_EQUAL_UINT32(HA_TTL_MIN_S, s_out.field[0].ttl_s);
    TEST_ASSERT_EQUAL_STRING("one", s_out.field[1].label); /* up to a control character */
    TEST_ASSERT_EQUAL_UINT8(0, s_out.field[1].precision);
    TEST_ASSERT_EQUAL_UINT32(HA_TTL_MAX_S, s_out.field[1].ttl_s);
}

static void test_deep_nesting_is_refused_before_parsing(void)
{
    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"x\": ");
    memset(s_json + at, '[', 40);
    memset(s_json + at + 40, ']', 40);
    snprintf(s_json + at + 80, sizeof(s_json) - at - 80, "}");
    TEST_ASSERT_FALSE(ha_fields_from_json(s_json, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
}

/* The largest file: 32 fields with every text at its longest; HA_FIELDS_JSON_MAX holds it. */
static void test_the_largest_file_fits(void)
{
    ha_fields_t f = { .count = HA_FIELDS_MAX };
    for (int i = 0; i < HA_FIELDS_MAX; i++) {
        ha_field_t *x = &f.field[i];
        snprintf(x->key, sizeof(x->key), "k%022d", i);
        memset(x->label, 'L', sizeof(x->label) - 1);
        memset(x->unit, 'u', sizeof(x->unit) - 1);
        memset(x->topic, 't', sizeof(x->topic) - 1);
        memset(x->json_path, 'p', sizeof(x->json_path) - 1);
        x->kind = HA_KIND_NUMBER;
        x->precision = 3;
        x->ttl_s = HA_TTL_MAX_S;
    }
    size_t n = ha_fields_to_json(&f, s_json, sizeof(s_json));
    TEST_ASSERT_TRUE(n > 0);
    printf("the largest mqtt_fields.json: %u bytes of %u\n", (unsigned)n, (unsigned)sizeof(s_json));
    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(s_json, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&f, &s_out, sizeof(f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_spec_example_parses);
    RUN_TEST(test_a_field_needs_only_a_key_and_a_topic);
    RUN_TEST(test_it_round_trips);
    RUN_TEST(test_keys_are_short_and_plain);
    RUN_TEST(test_structural_errors_refuse_the_file);
    RUN_TEST(test_at_most_32_fields);
    RUN_TEST(test_the_rest_is_cut_or_clamped);
    RUN_TEST(test_deep_nesting_is_refused_before_parsing);
    RUN_TEST(test_the_largest_file_fits);
    return UNITY_END();
}
